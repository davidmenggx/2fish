#include "orderbook_store.hpp"
#include "common/containers/ring_buffer.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <variant>

OrderbookStore::OrderbookStore()
    : yes_buffer_{std::make_unique<RingBuffer<
          OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>()},
      yes_live_snapshot_{std::make_unique<OrderbookStoreSnapshot>()},
      no_buffer_{
          std::make_unique<RingBuffer<OrderbookStoreSnapshot,
                                      constants::ORDERBOOK_HISTORY_STEPS>>()},
      no_live_snapshot_{std::make_unique<OrderbookStoreSnapshot>()} {}

[[nodiscard]] bool
OrderbookStore::recordOrderbookMessage(WebsocketMessage &message) {
  if (message.sequence_id_ != last_message_seq_ + 1) {
    last_message_seq_ = message.sequence_id_;
    return false; // Signify that we have missed a message
  }

  if (message.message_type_ == WebsocketMessage::MessageType::OrderbookDelta)
    recordOrderbookDelta(message);
  else if (message.message_type_ ==
           WebsocketMessage::MessageType::OrderbookSnapshot)
    recordOrderbookSnapshot(message);

  ++last_message_seq_;
  return true;
}

void OrderbookStore::recordOrderbookDelta(WebsocketMessage &message) {
  OrderbookDeltaMessage *message_body{
      std::get_if<OrderbookDeltaMessage>(&message.body_)};
  if (!message_body)
    return;

  if (message_body->price_cents_ > 100 || message_body->price_cents_ < 0)
    return;

  int64_t this_message_timestamp_ms =
      (message_body->timestamp_ms_ /
       constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
      constants::ORDERBOOK_HISTORY_GRANULARITY_MS;

  switch (message_body->side_) {
  case Side::Yes:
    // If this message falls beyond the time interval of the last message,
    // save the last message and start again.
    if (message_body->timestamp_ms_ >
        ((yes_live_snapshot_->start_timestamp_ms_ /
          constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
             constants::ORDERBOOK_HISTORY_GRANULARITY_MS +
         constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
      yes_buffer_->push(*yes_live_snapshot_);
      clearLiveYesSnapshot();
      yes_live_snapshot_->start_timestamp_ms_ = this_message_timestamp_ms;
    }
    if (earliest_yes_timestamp_ms_ == std::numeric_limits<int64_t>::min()) {
      earliest_yes_timestamp_ms_ = this_message_timestamp_ms;

    } else {
      earliest_yes_timestamp_ms_ =
          std::min(earliest_yes_timestamp_ms_, this_message_timestamp_ms);
    }
    if (message_body->timestamp_ms_ < earliest_yes_timestamp_ms_ ||
        earliest_yes_timestamp_ms_ == std::numeric_limits<int64_t>::min())
      earliest_yes_timestamp_ms_ =
          (message_body->timestamp_ms_ /
           constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
          constants::ORDERBOOK_HISTORY_GRANULARITY_MS;
    yes_live_snapshot_->dollars_[message_body->price_cents_] +=
        message_body->delta_;
    break;
  case Side::No:
    // If this message falls beyond the time interval of the last message,
    // save the last message and start again.
    if (message_body->timestamp_ms_ >
        ((no_live_snapshot_->start_timestamp_ms_ /
          constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
             constants::ORDERBOOK_HISTORY_GRANULARITY_MS +
         constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
      no_buffer_->push(*no_live_snapshot_);
      clearLiveNoSnapshot();
      no_live_snapshot_->start_timestamp_ms_ =
          (message_body->timestamp_ms_ /
           constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
          constants::ORDERBOOK_HISTORY_GRANULARITY_MS;
    }
    if (earliest_no_timestamp_ms_ == std::numeric_limits<int64_t>::min())
      earliest_no_timestamp_ms_ = no_live_snapshot_->start_timestamp_ms_;
    no_live_snapshot_->dollars_[message_body->price_cents_] +=
        message_body->delta_;
    break;
  }
}

void OrderbookStore::recordOrderbookSnapshot(WebsocketMessage &message) {
  OrderbookSnapshotMessage *message_body{
      std::get_if<OrderbookSnapshotMessage>(&message.body_)};
  if (!message_body)
    return;

  // Ideally we would like to fetch the exchange timestamp for this,
  // but unfortunately the snapshot messages do not include this field.
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  int64_t timestamp_ms{static_cast<int64_t>(millis)};

  // If this message falls beyond the time interval of the last message,
  // save the last message and start again.
  if (timestamp_ms > ((yes_live_snapshot_->start_timestamp_ms_ /
                       constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
                          constants::ORDERBOOK_HISTORY_GRANULARITY_MS +
                      constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
    yes_buffer_->push(*yes_live_snapshot_);
  }

  if (timestamp_ms > ((no_live_snapshot_->start_timestamp_ms_ /
                       constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
                          constants::ORDERBOOK_HISTORY_GRANULARITY_MS +
                      constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
    no_buffer_->push(*no_live_snapshot_);
  }

  // Take snapshots as a ground truth. Clear unconditionally
  clearLiveYesSnapshot();
  clearLiveNoSnapshot();
  yes_live_snapshot_->start_timestamp_ms_ =
      (timestamp_ms / constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
      constants::ORDERBOOK_HISTORY_GRANULARITY_MS;
  no_live_snapshot_->start_timestamp_ms_ =
      (timestamp_ms / constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
      constants::ORDERBOOK_HISTORY_GRANULARITY_MS;

  if (earliest_yes_timestamp_ms_ == std::numeric_limits<int64_t>::min())
    earliest_yes_timestamp_ms_ = timestamp_ms;

  if (earliest_no_timestamp_ms_ == std::numeric_limits<int64_t>::min())
    earliest_no_timestamp_ms_ = timestamp_ms;

  yes_live_snapshot_->dollars_ = std::move(message_body->yes_dollars_);
  no_live_snapshot_->dollars_ = std::move(message_body->no_dollars_);
}

void OrderbookStore::clearLiveYesSnapshot() {
  std::fill(yes_live_snapshot_->dollars_.begin(),
            yes_live_snapshot_->dollars_.end(), 0.0);
  yes_live_snapshot_->start_timestamp_ms_ = 0;
}

void OrderbookStore::clearLiveNoSnapshot() {
  std::fill(no_live_snapshot_->dollars_.begin(),
            no_live_snapshot_->dollars_.end(), 0.0);
  no_live_snapshot_->start_timestamp_ms_ = 0;
}
