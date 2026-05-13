#include "candlestick_store.hpp"
#include "common/containers/seqlock_wrapper.hpp"
#include "common/containers/swmr_map.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/utils/compute_time_bucket.hpp"
#include "constants.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <variant>

#include <iostream>

CandlestickStore::CandlestickStore()
    : yes_live_candlestick_{std::make_unique<
          SeqLockWrapper<CandlestickStoreSnapshot>>()},
      yes_map_{
          std::make_unique<SwmrMap<int64_t, CandlestickStoreSnapshot,
                                   constants::CANDLESTICK_HISTORY_STEPS>>()},
      no_live_candlestick_{
          std::make_unique<SeqLockWrapper<CandlestickStoreSnapshot>>()},
      no_map_{
          std::make_unique<SwmrMap<int64_t, CandlestickStoreSnapshot,
                                   constants::CANDLESTICK_HISTORY_STEPS>>()} {
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

  std::cout << "Got a trade message\n";
  ++last_message_seq_;

  switch (message_body->taker_side_) {
  case Side::Yes:
    yes_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
      // TODO: Think about what should happen if a trade comes in late
      if (message_body->timestamp_ms_ < store.start_timestamp_ms_)
        return;
      if (store.open_ == 255) {
        store.open_ = message_body->yes_price_cents_;
      }
      if (store.high_ == 255) {
        store.high_ = message_body->yes_price_cents_;
      } else {
        store.high_ = std::max(store.high_, message_body->yes_price_cents_);
      }
      if (store.low_ == 255) {
        store.low_ = message_body->yes_price_cents_;
      } else {
        store.low_ = std::min(store.low_, message_body->yes_price_cents_);
      }
      store.close_ = message_body->yes_price_cents_;
    });
    break;
  case Side::No:
    no_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
      if (message_body->timestamp_ms_ < store.start_timestamp_ms_)
        return;
      if (store.open_ == 255) {
        store.open_ = message_body->no_price_cents_;
      }
      if (store.high_ == 255) {
        store.high_ = message_body->no_price_cents_;
      } else {
        store.high_ = std::max(store.high_, message_body->no_price_cents_);
      }
      if (store.low_ == 255) {
        store.low_ = message_body->no_price_cents_;
      } else {
        store.low_ = std::min(store.low_, message_body->no_price_cents_);
      }
      store.close_ = message_body->no_price_cents_;
    });
    break;
  }
  return true;
}

void CandlestickStore::tryRolloverYesCandlestick(int64_t now_ms) {
  // TODO: We really should cache the start timestamp of the live candle,
  // I think this constant write is causing some contention.
  int64_t target_interval_start_ms{
      computeTimeBucket(now_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS)};
  yes_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
    while (store.start_timestamp_ms_ < target_interval_start_ms) {
      yes_map_->put(
          computeTimeBucket(store.start_timestamp_ms_,
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
  });
}

void CandlestickStore::tryRolloverNoCandlestick(int64_t now_ms) {
  int64_t target_interval_start_ms{
      computeTimeBucket(now_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS)};

  no_live_candlestick_->write([&](CandlestickStoreSnapshot &store) {
    while (store.start_timestamp_ms_ < target_interval_start_ms) {
      no_map_->put(
          computeTimeBucket(store.start_timestamp_ms_,
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
  });
}

[[nodiscard]] std::optional<CandlestickStoreSnapshot>
CandlestickStore::get(int64_t query_timestamp_ms, Side side) {
  if (invalid_state_.load(std::memory_order_acquire) &&
      query_timestamp_ms >
          last_valid_timestamp_ms_.load(std::memory_order_acquire)) {
    return std::nullopt;
  }

  switch (side) {
  case Side::Yes: {
    // Fast lock-free read
    CandlestickStoreSnapshot live_snapshot = yes_live_candlestick_->read(
        [](const CandlestickStoreSnapshot &store) { return store; });
    if (query_timestamp_ms >= live_snapshot.start_timestamp_ms_ &&
        live_snapshot.open_ != 255 && live_snapshot.high_ != 255 &&
        live_snapshot.low_ != 255 && live_snapshot.close_ != 255)
      return live_snapshot;
    return yes_map_->get(computeTimeBucket(
        query_timestamp_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS));
    break;
  }
  case Side::No: {
    CandlestickStoreSnapshot live_snapshot = no_live_candlestick_->read(
        [](const CandlestickStoreSnapshot &store) { return store; });
    if (query_timestamp_ms >= live_snapshot.start_timestamp_ms_ &&
        live_snapshot.open_ != 255 && live_snapshot.high_ != 255 &&
        live_snapshot.low_ != 255 && live_snapshot.close_ != 255)
      return live_snapshot;
    return no_map_->get(computeTimeBucket(
        query_timestamp_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS));
    break;
  }
  }
  return std::nullopt;
}
