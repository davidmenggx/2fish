#include "orderbook_store.hpp"
#include "common/containers/ring_buffer.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <variant>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

OrderbookStore::OrderbookStore()
    : buffer_{std::make_unique<RingBuffer<
          OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>()},
      live_snapshot_{std::make_unique<OrderbookStoreSnapshot>()} {}

[[nodiscard]] bool OrderbookStore::recordOrderbookMessage(WebsocketMessage &message) {
  if (message.message_type_ == WebsocketMessage::MessageType::OrderbookDelta)
    recordOrderbookDelta(message);
  else if (message.message_type_ ==
           WebsocketMessage::MessageType::OrderbookSnapshot)
    recordOrderbookSnapshot(message);

  if (message.sequence_id_ != last_message_seq_ + 1) {
    last_message_seq_ = message.sequence_id_;
    return false; // Signify that we have missed a message
  }

  return true;
}

void OrderbookStore::recordOrderbookDelta(WebsocketMessage &message) {
  OrderbookDeltaMessage *message_body{
      std::get_if<OrderbookDeltaMessage>(&message.body_)};
  if (!message_body)
    return;

  // If this message falls beyond the time interval of the last message,
  // save the last message and start again.
  if (message_body->timestamp_ms_ >
      ((live_snapshot_->start_timestamp_ms_ /
        constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
           constants::ORDERBOOK_HISTORY_GRANULARITY_MS +
       constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
    buffer_->push(*live_snapshot_);
    clearLiveSnapshot();
  }

  live_snapshot_->start_timestamp_ms_ =
      (message_body->timestamp_ms_ /
       constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
      constants::ORDERBOOK_HISTORY_GRANULARITY_MS;

  switch (message_body->side_) {
  case Side::Yes:
    live_snapshot_->yes_dollars_[message_body->price_cents_] +=
        message_body->delta_;
    break;
  case Side::No:
    live_snapshot_->no_dollars_[message_body->price_cents_] +=
        message_body->delta_;
    break;
  }
}

void OrderbookStore::recordOrderbookSnapshot(WebsocketMessage &message) {
  OrderbookSnapshotMessage *message_body{
      std::get_if<OrderbookSnapshotMessage>(&message.body_)};
  if (!message_body)
    return;

  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  int64_t timestamp_ms{static_cast<int64_t>(millis)};

  // If this message falls beyond the time interval of the last message,
  // save the last message and start again.
  if (timestamp_ms > ((live_snapshot_->start_timestamp_ms_ /
                       constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
                          constants::ORDERBOOK_HISTORY_GRANULARITY_MS +
                      constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
    buffer_->push(*live_snapshot_);
  }
  clearLiveSnapshot();

  live_snapshot_->start_timestamp_ms_ =
      (timestamp_ms / constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
      constants::ORDERBOOK_HISTORY_GRANULARITY_MS;

  live_snapshot_->yes_dollars_ = std::move(message_body->yes_dollars_);
  live_snapshot_->no_dollars_ = std::move(message_body->no_dollars_);
}

void OrderbookStore::clearLiveSnapshot() {
  std::fill(live_snapshot_->yes_dollars_.begin(),
            live_snapshot_->yes_dollars_.end(), 0.0);
  std::fill(live_snapshot_->no_dollars_.begin(),
            live_snapshot_->no_dollars_.end(), 0.0);
  live_snapshot_->start_timestamp_ms_ = 0;
}
