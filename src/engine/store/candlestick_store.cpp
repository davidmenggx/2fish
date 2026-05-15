#include "candlestick_store.hpp"
#include "common/containers/seqlock_wrapper.hpp"
#include "common/containers/swmr_map.hpp"
#include "common/core/rest_data_types.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/utils/compute_time_bucket.hpp"
#include "common/utils/set_query_params.hpp"
#include "config.hpp"
#include "constants.hpp"

#include "moodycamel/concurrentqueue.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <variant>

#include <iostream>

CandlestickStore::CandlestickStore(
    moodycamel::ConcurrentQueue<RestMessage> &rest_query_queue, Config config)
    : yes_live_candlestick_{std::make_unique<
          SeqLockWrapper<CandlestickStoreSnapshot>>()},
      yes_map_{
          std::make_unique<SwmrMap<int64_t, CandlestickStoreSnapshot,
                                   constants::CANDLESTICK_HISTORY_STEPS>>()},
      no_live_candlestick_{
          std::make_unique<SeqLockWrapper<CandlestickStoreSnapshot>>()},
      no_map_{
          std::make_unique<SwmrMap<int64_t, CandlestickStoreSnapshot,
                                   constants::CANDLESTICK_HISTORY_STEPS>>()},
      rest_client_{rest_query_queue},
      candlestick_fetch_history_{std::make_unique<
          SwmrMap<int64_t, int64_t, constants::PAST_MESSAGE_LOOKUP_SIZE>>()},
      config_{config} {
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
  int64_t now_ms_bucket{
      computeTimeBucket(now_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS)};
  yes_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
    store.start_timestamp_ms_ = now_ms_bucket;
  });
  no_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
    store.start_timestamp_ms_ = now_ms_bucket;
  });
}

[[nodiscard]] bool
CandlestickStore::recordTradeMessageWs(WebsocketMessage &message) {
  TradeMessageWs *message_body{std::get_if<TradeMessageWs>(&message.body_)};
  if (!message_body)
    return true; // Still true here because we didn't miss a message

  if (message.sequence_id_ != last_message_seq_ + 1) {
    last_message_seq_ = message.sequence_id_;
    return false; // Signify that we have missed a message
  }

  ++last_message_seq_;

  auto update_candlestick = [message_body](CandlestickStoreSnapshot &store,
                                           uint8_t price_cents) {
    if (message_body->timestamp_ms_ < store.start_timestamp_ms_)
      return;
    if (store.open_ == 255)
      store.open_ = price_cents;
    if (store.high_ == 255)
      store.high_ = price_cents;
    else
      store.high_ = std::max(store.high_, price_cents);
    if (store.low_ == 255)
      store.low_ = price_cents;
    else
      store.low_ = std::min(store.low_, price_cents);
    store.close_ = price_cents;
  };

  switch (message_body->taker_side_) {
  case Side::Yes:
    yes_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
      update_candlestick(store, message_body->yes_price_cents_);
    });
    break;
  case Side::No:
    no_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
      update_candlestick(store, message_body->no_price_cents_);
    });
    break;
  case Side::Unknown:
    break;
  }

  tryRolloverCandlesticks(message_body->timestamp_ms_);
  return true;
}

void CandlestickStore::tryRolloverCandlesticks(int64_t now_ms) {
  int64_t target_interval_start_ms{
      computeTimeBucket(now_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS)};

  auto perform_rollover = [target_interval_start_ms](
                              CandlestickStoreSnapshot &store, auto &map) {
    while (store.start_timestamp_ms_ < target_interval_start_ms) {
      map->put(computeTimeBucket(store.start_timestamp_ms_,
                                 constants::CANDLESTICK_HISTORY_GRANULARITY_MS),
               store);

      int32_t last_close{store.close_};

      // 255 is a sentinel value for no data
      if (last_close != 255) {
        store.open_ = last_close;
        store.high_ = last_close;
        store.low_ = last_close;
        store.close_ = last_close;
      }

      store.start_timestamp_ms_ =
          computeTimeBucket(store.start_timestamp_ms_ +
                                constants::CANDLESTICK_HISTORY_GRANULARITY_MS,
                            constants::CANDLESTICK_HISTORY_GRANULARITY_MS);
    }
  };

  yes_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
    perform_rollover(store, yes_map_);
  });
  no_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
    perform_rollover(store, no_map_);
  });
}

[[nodiscard]] std::optional<CandlestickStoreSnapshot>
CandlestickStore::get(int64_t query_timestamp_ms, Side side) {
  if (invalid_state_.load(std::memory_order_acquire) /* &&
      query_timestamp_ms >
          last_valid_timestamp_ms_.load(std::memory_order_acquire)*/) {
    return std::nullopt;
  }

  auto fetch_snapshot =
      [&](auto &live_store, auto &history_map,
          Side fetch_side) -> std::optional<CandlestickStoreSnapshot> {
    CandlestickStoreSnapshot live_snapshot = live_store->read(
        [](const CandlestickStoreSnapshot &store) { return store; });

    if (query_timestamp_ms >= live_snapshot.start_timestamp_ms_ &&
        live_snapshot.open_ != 255 && live_snapshot.high_ != 255 &&
        live_snapshot.low_ != 255 && live_snapshot.close_ != 255)
      return live_snapshot;

    auto result = history_map->get(computeTimeBucket(
        query_timestamp_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS));

    if (!result)
      tryFetchHistoricalCandlestick(
          computeTimeBucket(query_timestamp_ms,
                            constants::CANDLESTICK_HISTORY_GRANULARITY_MS),
          fetch_side);

    return result;
  };

  switch (side) {
  case Side::Yes:
    // Fast lock-free read
    return fetch_snapshot(yes_live_candlestick_, yes_map_, Side::Yes);
  case Side::No:
    return fetch_snapshot(no_live_candlestick_, no_map_, Side::No);
  case Side::Unknown:
    break;
  }

  return std::nullopt;
}

void CandlestickStore::tryPatch(RestMessage &message) {
  if (!invalid_state_.load(std::memory_order_acquire))
    return;

  CandlestickMessageRest *message_body{
      std::get_if<CandlestickMessageRest>(&message.body_)};
  if (!message_body)
    return;

  CandlestickStoreSnapshot live_no_snapshot = no_live_candlestick_->read(
      [](const CandlestickStoreSnapshot &store) { return store; });

  CandlestickStoreSnapshot live_yes_snapshot = yes_live_candlestick_->read(
      [](const CandlestickStoreSnapshot &store) { return store; });

  int64_t last_valid_timestamp_ms{std::numeric_limits<int64_t>::min()};

  for (auto &candlestick : message_body->candlesticks_) {
    // TODO: The 60 comes from the 1 minute period interval, which could
    // change!!!!
    int64_t candlestick_start_timestamp_ms{(candlestick.end_period_ts_s_ - 60) *
                                           1000};
    last_valid_timestamp_ms =
        std::max(last_valid_timestamp_ms, candlestick_start_timestamp_ms);

    CandlestickStoreSnapshot this_yes_candlestick{
        .open_ = candlestick.open_cents_,
        .high_ = candlestick.high_cents_,
        .low_ = candlestick.low_cents_,
        .close_ = candlestick.close_cents_,
        .start_timestamp_ms_ = candlestick_start_timestamp_ms};

    if (candlestick_start_timestamp_ms >=
        live_yes_snapshot.start_timestamp_ms_) {
      yes_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
        store = this_yes_candlestick;
      });
    } else {
      yes_map_->put(candlestick_start_timestamp_ms, this_yes_candlestick);
    }

    // The no side is just the opposite of the yes side
    CandlestickStoreSnapshot this_no_candlestick{
        .open_ = static_cast<uint8_t>(100 - candlestick.open_cents_),
        .high_ = static_cast<uint8_t>(100 - candlestick.high_cents_),
        .low_ = static_cast<uint8_t>(100 - candlestick.low_cents_),
        .close_ = static_cast<uint8_t>(100 - candlestick.close_cents_),
        .start_timestamp_ms_ = candlestick_start_timestamp_ms};

    if (candlestick_start_timestamp_ms >=
        live_no_snapshot.start_timestamp_ms_) {
      no_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
        store = this_no_candlestick;
      });
    } else {
      no_map_->put(candlestick_start_timestamp_ms, this_no_candlestick);
    }
  }

  if (last_valid_timestamp_ms == std::numeric_limits<int64_t>::min())
    return;

  last_valid_timestamp_ms_.store(last_valid_timestamp_ms,
                                 std::memory_order_release);
  invalid_state_.store(false, std::memory_order_release);

  std::cout << "Applied a candlestick patch\n";
}

void CandlestickStore::tryFetchHistoricalCandlestick(int64_t query_timestamp_ms,
                                                     Side side) {
  // As this can be initiated simultaneously by multiple readers,
  // this function needs to be thread safe

  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();

  // Don't spam the request if one has recently been sent out
  std::optional<int64_t> last_queried_timestamp_ms{
      candlestick_fetch_history_->get(query_timestamp_ms)};
  if (last_queried_timestamp_ms && ((now_ms - *last_queried_timestamp_ms) <=
                                    constants::REST_MESSAGE_COOLDOWN_MS))
    return;

  candlestick_fetch_history_->put(query_timestamp_ms, now_ms);

  std::string host = "external-api.kalshi.com";

  std::string target =
      std::format("/trade-api/v2/series/{}/markets/{}/candlesticks",
                  config_.series_ticker_, config_.market_ticker_);

  setQueryParams(
      target, std::make_pair("start_ts", query_timestamp_ms / 1000),
      std::make_pair("end_ts", (query_timestamp_ms +
                                constants::CANDLESTICK_HISTORY_GRANULARITY_MS) /
                                   1000),
      std::make_pair("period_interval", 1));

  rest_client_.get(host, target);
}

void CandlestickStore::tryStoreHistorical(RestMessage &message) {
  std::cout << "A historical try store initialized\n";
  CandlestickMessageRest *message_body{
      std::get_if<CandlestickMessageRest>(&message.body_)};
  if (!message_body)
    return;

  for (auto &candlestick : message_body->candlesticks_) {
    int64_t candlestick_start_timestamp_ms{(candlestick.end_period_ts_s_ - 60) *
                                           1000};

    yes_map_->put(candlestick_start_timestamp_ms,
                  {.open_ = candlestick.open_cents_,
                   .high_ = candlestick.high_cents_,
                   .low_ = candlestick.low_cents_,
                   .close_ = candlestick.close_cents_,
                   .start_timestamp_ms_ = candlestick_start_timestamp_ms});

    // The no side is just the opposite of the yes side
    no_map_->put(
        candlestick_start_timestamp_ms,
        {.open_ = static_cast<uint8_t>(100 - candlestick.open_cents_),
         .high_ = static_cast<uint8_t>(100 - candlestick.high_cents_),
         .low_ = static_cast<uint8_t>(100 - candlestick.low_cents_),
         .close_ = static_cast<uint8_t>(100 - candlestick.close_cents_),
         .start_timestamp_ms_ = candlestick_start_timestamp_ms});
  }
}
