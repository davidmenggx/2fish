#include "candlestick_store.hpp"
#include "common/containers/swmr_map.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <memory>
#include <variant>

CandlestickStore::CandlestickStore()
    : yes_live_candlestick_{std::make_unique<CandlestickStoreSnapshot>()},
      yes_map_{
          std::make_unique<SwmrMap<int64_t, CandlestickStoreSnapshot,
                                   constants::CANDLESTICK_HISTORY_STEPS>>()},
      no_live_candlestick_{std::make_unique<CandlestickStoreSnapshot>()},
      no_map_{
          std::make_unique<SwmrMap<int64_t, CandlestickStoreSnapshot,
                                   constants::CANDLESTICK_HISTORY_STEPS>>()} {
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
  yes_live_candlestick_->start_timestamp_ms_ = now_ms;
  no_live_candlestick_->start_timestamp_ms_ = now_ms;
}

[[nodiscard]] bool
CandlestickStore::recordTradeMessage(WebsocketMessage &message) {
  TradeMessage *message_body{std::get_if<TradeMessage>(&message.body_)};
  if (!message_body)
    return true; // Still true here because we didn't miss a message

  if (message.sequence_id_ != last_message_seq_ + 1) {
    last_message_seq_ = message.sequence_id_;
    return false; // Signify that we have missed a message
  }

  // IMPORTANT: Consider the failure case when the trade message comes in late
  // and the heartbeat has already rolled over.

  switch (message_body->taker_side_) {
  case Side::Yes:
    // If this message falls beyond the time interval of the last message,
    // save the last message and start again.
    if (message_body->timestamp_ms_ >
        ((yes_live_candlestick_->start_timestamp_ms_ /
          constants::CANDLESTICK_HISTORY_GRANULARITY_MS) *
             constants::CANDLESTICK_HISTORY_GRANULARITY_MS +
         constants::CANDLESTICK_HISTORY_GRANULARITY_MS)) {
      yes_map_->put((yes_live_candlestick_->start_timestamp_ms_ /
                     constants::CANDLESTICK_HISTORY_GRANULARITY_MS),
                    *yes_live_candlestick_);
      clearLiveYesCandlestick();
      yes_live_candlestick_->start_timestamp_ms_ =
          (message_body->timestamp_ms_ /
           constants::CANDLESTICK_HISTORY_GRANULARITY_MS) *
          constants::CANDLESTICK_HISTORY_GRANULARITY_MS;
    }
    break;
  case Side::No:
    // If this message falls beyond the time interval of the last message,
    // save the last message and start again.
    if (message_body->timestamp_ms_ >
        ((no_live_candlestick_->start_timestamp_ms_ /
          constants::CANDLESTICK_HISTORY_GRANULARITY_MS) *
             constants::CANDLESTICK_HISTORY_GRANULARITY_MS +
         constants::CANDLESTICK_HISTORY_GRANULARITY_MS)) {
      no_map_->put((no_live_candlestick_->start_timestamp_ms_ /
                    constants::CANDLESTICK_HISTORY_GRANULARITY_MS),
                   *no_live_candlestick_);
      clearLiveNoCandlestick();
      no_live_candlestick_->start_timestamp_ms_ =
          (message_body->timestamp_ms_ /
           constants::CANDLESTICK_HISTORY_GRANULARITY_MS) *
          constants::CANDLESTICK_HISTORY_GRANULARITY_MS;
    }
    break;
  }

  updateLiveCandlestick(message_body);

  ++last_message_seq_;
  return true;
}

void CandlestickStore::clearLiveYesCandlestick() {
  yes_live_candlestick_->open_ = 255;
  yes_live_candlestick_->high_ = 255;
  yes_live_candlestick_->low_ = 255;
  yes_live_candlestick_->close_ = 255;
  yes_live_candlestick_->start_timestamp_ms_ = 0;
}

void CandlestickStore::clearLiveNoCandlestick() {
  no_live_candlestick_->open_ = 255;
  no_live_candlestick_->high_ = 255;
  no_live_candlestick_->low_ = 255;
  no_live_candlestick_->close_ = 255;
  no_live_candlestick_->start_timestamp_ms_ = 0;
}

void CandlestickStore::updateLiveCandlestick(const TradeMessage *message_body) {
  switch (message_body->taker_side_) {
  case Side::Yes:
    // 255 is a sentinel value after clear
    if (yes_live_candlestick_->open_ == 255) {
      yes_live_candlestick_->open_ = message_body->yes_price_cents_;
    }
    if (yes_live_candlestick_->high_ == 255) {
      yes_live_candlestick_->high_ = message_body->yes_price_cents_;
    } else {
      yes_live_candlestick_->high_ = std::max(yes_live_candlestick_->high_,
                                              message_body->yes_price_cents_);
    }
    if (yes_live_candlestick_->low_ == 255) {
      yes_live_candlestick_->low_ = message_body->yes_price_cents_;
    } else {
      yes_live_candlestick_->low_ =
          std::min(yes_live_candlestick_->low_, message_body->yes_price_cents_);
    }
    yes_live_candlestick_->close_ = message_body->yes_price_cents_;
    break;
  case Side::No:
    // 255 is a sentinel value after clear
    if (no_live_candlestick_->open_ == 255) {
      no_live_candlestick_->open_ = message_body->no_price_cents_;
    }
    if (no_live_candlestick_->high_ == 255) {
      no_live_candlestick_->high_ = message_body->no_price_cents_;
    } else {
      no_live_candlestick_->high_ =
          std::max(no_live_candlestick_->high_, message_body->no_price_cents_);
    }
    if (no_live_candlestick_->low_ == 255) {
      no_live_candlestick_->low_ = message_body->no_price_cents_;
    } else {
      no_live_candlestick_->low_ =
          std::min(no_live_candlestick_->low_, message_body->no_price_cents_);
    }
    no_live_candlestick_->close_ = message_body->no_price_cents_;
    break;
  }
}

void CandlestickStore::tryRolloverYesCandlestick(int64_t now_ms) {
  int64_t target_interval_start_ms =
      (now_ms / constants::CANDLESTICK_HISTORY_GRANULARITY_MS) *
      constants::CANDLESTICK_HISTORY_GRANULARITY_MS;

  while (yes_live_candlestick_->start_timestamp_ms_ <
         target_interval_start_ms) {

    int64_t completed_interval_key =
        yes_live_candlestick_->start_timestamp_ms_ /
        constants::CANDLESTICK_HISTORY_GRANULARITY_MS;
    yes_map_->put(completed_interval_key, *yes_live_candlestick_);

    int32_t last_close{yes_live_candlestick_->close_};

    // 255 is a sentinel value for no data
    if (last_close != 255) {
      yes_live_candlestick_->open_ = last_close;
      yes_live_candlestick_->high_ = last_close;
      yes_live_candlestick_->low_ = last_close;
      yes_live_candlestick_->close_ = last_close;
    }

    yes_live_candlestick_->start_timestamp_ms_ +=
        constants::CANDLESTICK_HISTORY_GRANULARITY_MS;
  }
}

void CandlestickStore::tryRolloverNoCandlestick(int64_t now_ms) {
  int64_t target_interval_start_ms =
      (now_ms / constants::CANDLESTICK_HISTORY_GRANULARITY_MS) *
      constants::CANDLESTICK_HISTORY_GRANULARITY_MS;

  while (no_live_candlestick_->start_timestamp_ms_ < target_interval_start_ms) {

    int64_t completed_interval_key =
        no_live_candlestick_->start_timestamp_ms_ /
        constants::CANDLESTICK_HISTORY_GRANULARITY_MS;
    no_map_->put(completed_interval_key, *no_live_candlestick_);

    int32_t last_close{no_live_candlestick_->close_};

    // 255 is a sentinel value for no data
    if (last_close != 255) {
      no_live_candlestick_->open_ = last_close;
      no_live_candlestick_->high_ = last_close;
      no_live_candlestick_->low_ = last_close;
      no_live_candlestick_->close_ = last_close;
    }

    no_live_candlestick_->start_timestamp_ms_ +=
        constants::CANDLESTICK_HISTORY_GRANULARITY_MS;
  }
}
