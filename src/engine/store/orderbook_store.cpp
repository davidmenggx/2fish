#include "orderbook_store.hpp"
#include "common/containers/ring_buffer.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

OrderbookStore::OrderbookStore()
    : yes_buffer_{std::make_unique<RingBuffer<
          OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>()},
      yes_live_snapshot_{std::make_unique<OrderbookStoreSnapshot>()},
      no_buffer_{
          std::make_unique<RingBuffer<OrderbookStoreSnapshot,
                                      constants::ORDERBOOK_HISTORY_STEPS>>()},
      no_live_snapshot_{std::make_unique<OrderbookStoreSnapshot>()} {
  yes_fetch_buffer_.reserve(constants::ORDERBOOK_HISTORY_STEPS);
  no_fetch_buffer_.reserve(constants::ORDERBOOK_HISTORY_STEPS);
}

[[nodiscard]] bool
OrderbookStore::recordOrderbookMessage(WebsocketMessage &message) {
  if (message.sequence_id_ != last_message_seq_ + 1 || invalid_state_) {
    last_message_seq_ = message.sequence_id_;
    invalid_state_ = true;
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
      yes_live_snapshot_->start_timestamp_ms_ = this_message_timestamp_ms;
    }
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
      no_live_snapshot_->start_timestamp_ms_ =
          (message_body->timestamp_ms_ /
           constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
          constants::ORDERBOOK_HISTORY_GRANULARITY_MS;
    }
    no_live_snapshot_->dollars_[message_body->price_cents_] +=
        message_body->delta_;
    break;
  }

  if (yes_live_snapshot_->dollars_[message_body->price_cents_] < -0.0001) {
    invalid_state_ = true;
    throw std::logic_error(
        std::format("CRITICAL: Orderbook volume cannot be negative, Yes market "
                    "has value {} at {} cents\n",
                    yes_live_snapshot_->dollars_[message_body->price_cents_],
                    message_body->price_cents_));
  }
  if (std::abs(yes_live_snapshot_->dollars_[message_body->price_cents_]) <
      0.0001) {
    yes_live_snapshot_->dollars_[message_body->price_cents_] = 0.0;
  }

  if (no_live_snapshot_->dollars_[message_body->price_cents_] < -0.0001) {
    invalid_state_ = true;
    throw std::logic_error(
        std::format("CRITICAL: Orderbook volume cannot be negative, No market "
                    "has value {} at {} cents\n",
                    no_live_snapshot_->dollars_[message_body->price_cents_],
                    message_body->price_cents_));
  }
  if (std::abs(no_live_snapshot_->dollars_[message_body->price_cents_]) <
      0.0001) {
    no_live_snapshot_->dollars_[message_body->price_cents_] = 0.0;
  }

  last_valid_timestamp_ms_ = message_body->timestamp_ms_;
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

[[nodiscard]] std::optional<OrderbookStoreSnapshot>
OrderbookStore::get(int64_t query_timestamp_ms, Side side) {
  if (invalid_state_ && query_timestamp_ms > last_valid_timestamp_ms_) {
    return std::nullopt;
  }
  switch (side) {
  case Side::Yes: {
    std::optional<OrderbookStoreSnapshot> earliest_yes_message{
        yes_buffer_->get(0)};
    if (earliest_yes_message &&
        query_timestamp_ms < earliest_yes_message->start_timestamp_ms_)
      return std::nullopt;
    if (query_timestamp_ms >= yes_live_snapshot_->start_timestamp_ms_)
      return *yes_live_snapshot_;

    yes_fetch_buffer_.clear();
    yes_buffer_->copy_to(yes_fetch_buffer_);

    auto it = std::upper_bound(
        yes_fetch_buffer_.begin(), yes_fetch_buffer_.end(), query_timestamp_ms,
        [](int64_t query, const OrderbookStoreSnapshot &obj) {
          return query < obj.start_timestamp_ms_;
        });
    if (it == yes_fetch_buffer_.begin())
      return std::nullopt;
    return *std::prev(it);
  }
  case Side::No: {
    std::optional<OrderbookStoreSnapshot> earliest_no_message{
        no_buffer_->get(0)};
    if (earliest_no_message &&
        query_timestamp_ms < earliest_no_message->start_timestamp_ms_)
      return std::nullopt;
    if (query_timestamp_ms >= no_live_snapshot_->start_timestamp_ms_)
      return *no_live_snapshot_;

    no_fetch_buffer_.clear();
    no_buffer_->copy_to(no_fetch_buffer_);

    auto it = std::upper_bound(
        no_fetch_buffer_.begin(), no_fetch_buffer_.end(), query_timestamp_ms,
        [](int64_t query, const OrderbookStoreSnapshot &obj) {
          return query < obj.start_timestamp_ms_;
        });
    if (it == no_fetch_buffer_.begin())
      return std::nullopt;
    return *std::prev(it);
  }
  }
  return std::nullopt;
}
