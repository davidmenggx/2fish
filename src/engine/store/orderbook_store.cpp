#include "orderbook_store.hpp"
#include "common/containers/ring_buffer.hpp"
#include "common/containers/seqlock_wrapper.hpp"
#include "common/core/rest_data_types.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/utils/compute_time_bucket.hpp"
#include "constants.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include <iostream>

OrderbookStore::OrderbookStore()
    : yes_buffer_{std::make_unique<RingBuffer<
          OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>()},
      yes_live_snapshot_{
          std::make_unique<SeqLockWrapper<OrderbookStoreSnapshot>>()},
      no_buffer_{
          std::make_unique<RingBuffer<OrderbookStoreSnapshot,
                                      constants::ORDERBOOK_HISTORY_STEPS>>()},
      no_live_snapshot_{
          std::make_unique<SeqLockWrapper<OrderbookStoreSnapshot>>()} {}

[[nodiscard]] bool
OrderbookStore::recordOrderbookMessage(WebsocketMessage &message) {
  if (state_patched_.load(std::memory_order_acquire)) {
    last_message_seq_ = message.sequence_id_;
    state_patched_.store(false, std::memory_order_release);
  } else if (message.sequence_id_ != last_message_seq_ + 1 ||
             invalid_state_.load(std::memory_order_acquire)) {
    last_message_seq_ = message.sequence_id_;
    invalid_state_.store(true, std::memory_order_release);
    std::cerr << "Invalid state triggered\n";
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
  OrderbookDeltaMessageWs *message_body{
      std::get_if<OrderbookDeltaMessageWs>(&message.body_)};
  if (!message_body)
    return;

  if (message_body->price_cents_ > 100 || message_body->price_cents_ < 0)
    return;

  int64_t this_message_timestamp_ms =
      (message_body->timestamp_ms_ /
       constants::ORDERBOOK_HISTORY_GRANULARITY_MS) *
      constants::ORDERBOOK_HISTORY_GRANULARITY_MS;

  // State variables to safely defer exceptions outside the lock
  bool trigger_critical_error{false};
  double error_value{0.0};
  const char *error_side = "";

  switch (message_body->side_) {
  case Side::Yes:
    yes_live_snapshot_->write([&](OrderbookStoreSnapshot &store) {
      // If this message falls beyond the time interval of the last message,
      // save the last message and start again.
      if (message_body->timestamp_ms_ >
          (computeTimeBucket(store.start_timestamp_ms_,
                             constants::ORDERBOOK_HISTORY_GRANULARITY_MS) +
           constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
        yes_buffer_->push(store);
        store.start_timestamp_ms_ = this_message_timestamp_ms;
      }

      // Apply delta
      store.dollars_[message_body->price_cents_] += message_body->delta_;

      // Validate and clean up floating point drift
      if (store.dollars_[message_body->price_cents_] < -0.0001) {
        trigger_critical_error = true;
        error_value = store.dollars_[message_body->price_cents_];
        error_side = "Yes";
      } else if (std::abs(store.dollars_[message_body->price_cents_]) <
                 0.0001) {
        store.dollars_[message_body->price_cents_] = 0.0;
      }
    });
    break;

  case Side::No:
    no_live_snapshot_->write([&](OrderbookStoreSnapshot &store) {
      // If this message falls beyond the time interval of the last message,
      // save the last message and start again.
      if (message_body->timestamp_ms_ >
          (computeTimeBucket(store.start_timestamp_ms_,
                             constants::ORDERBOOK_HISTORY_GRANULARITY_MS) +
           constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
        no_buffer_->push(store);
        store.start_timestamp_ms_ = this_message_timestamp_ms;
      }

      // Apply delta
      store.dollars_[message_body->price_cents_] += message_body->delta_;

      // Validate and clean up floating point drift
      if (store.dollars_[message_body->price_cents_] < -0.0001) {
        trigger_critical_error = true;
        error_value = store.dollars_[message_body->price_cents_];
        error_side = "No";
      } else if (std::abs(store.dollars_[message_body->price_cents_]) <
                 0.0001) {
        store.dollars_[message_body->price_cents_] = 0.0;
      }
    });
    break;
  }

  if (trigger_critical_error) {
    invalid_state_.store(true, std::memory_order_release);
    throw std::logic_error(
        std::format("CRITICAL: Orderbook volume cannot be negative, {} market "
                    "has value {} at {} cents\n",
                    error_side, error_value, message_body->price_cents_));
  }

  last_valid_timestamp_ms_.store(message_body->timestamp_ms_,
                                 std::memory_order_release);
}

void OrderbookStore::recordOrderbookSnapshot(WebsocketMessage &message) {
  OrderbookSnapshotMessageWs *message_body{
      std::get_if<OrderbookSnapshotMessageWs>(&message.body_)};
  if (!message_body)
    return;

  // Ideally we would like to fetch the exchange timestamp for this,
  // but unfortunately the snapshot messages do not include this field.
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  int64_t timestamp_ms{static_cast<int64_t>(millis)};

  yes_live_snapshot_->write([&](OrderbookStoreSnapshot &store) {
    if (timestamp_ms >
        (computeTimeBucket(store.start_timestamp_ms_,
                           constants::ORDERBOOK_HISTORY_GRANULARITY_MS) +
         constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
      yes_buffer_->push(store);
    }

    store.dollars_.fill(0.0);

    store.start_timestamp_ms_ = computeTimeBucket(
        timestamp_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS);

    store.dollars_ = std::move(message_body->yes_dollars_);
  });

  no_live_snapshot_->write([&](OrderbookStoreSnapshot &store) {
    if (timestamp_ms >
        (computeTimeBucket(store.start_timestamp_ms_,
                           constants::CANDLESTICK_HISTORY_GRANULARITY_MS) +
         constants::ORDERBOOK_HISTORY_GRANULARITY_MS)) {
      no_buffer_->push(store);
    }

    store.dollars_.fill(0.0);

    store.start_timestamp_ms_ = computeTimeBucket(
        timestamp_ms, constants::CANDLESTICK_HISTORY_GRANULARITY_MS);

    store.dollars_ = std::move(message_body->no_dollars_);
  });
}

[[nodiscard]] std::optional<OrderbookStoreSnapshot>
OrderbookStore::get(int64_t query_timestamp_ms, Side side) {
  if (invalid_state_.load(std::memory_order_acquire) &&
      query_timestamp_ms >
          last_valid_timestamp_ms_.load(std::memory_order_acquire)) {
    return std::nullopt;
  }

  switch (side) {
  case Side::Yes: {
    std::optional<OrderbookStoreSnapshot> earliest_yes_message{
        yes_buffer_->get(0)};
    if (earliest_yes_message &&
        query_timestamp_ms < earliest_yes_message->start_timestamp_ms_)
      return std::nullopt;

    // Fast lock-free read
    OrderbookStoreSnapshot live_snapshot = yes_live_snapshot_->read(
        [](const OrderbookStoreSnapshot &store) { return store; });

    if (query_timestamp_ms >= live_snapshot.start_timestamp_ms_)
      return live_snapshot;

    return yes_buffer_->prev_upper_bound(
        query_timestamp_ms,
        [](int64_t query, const OrderbookStoreSnapshot &obj) {
          return query < obj.start_timestamp_ms_;
        });
  }
  case Side::No: {
    std::optional<OrderbookStoreSnapshot> earliest_no_message{
        no_buffer_->get(0)};
    if (earliest_no_message &&
        query_timestamp_ms < earliest_no_message->start_timestamp_ms_)
      return std::nullopt;

    // Fast lock-free read
    OrderbookStoreSnapshot live_snapshot = no_live_snapshot_->read(
        [](const OrderbookStoreSnapshot &store) { return store; });

    if (query_timestamp_ms >= live_snapshot.start_timestamp_ms_)
      return live_snapshot;

    return no_buffer_->prev_upper_bound(
        query_timestamp_ms,
        [](int64_t query, const OrderbookStoreSnapshot &obj) {
          return query < obj.start_timestamp_ms_;
        });
  }
  }
  return std::nullopt;
}

void OrderbookStore::tryPatch(RestMessage &message) {
  if (!invalid_state_.load(std::memory_order_acquire))
    return;

  OrderbookSnapshotMessageRest *message_body{
      std::get_if<OrderbookSnapshotMessageRest>(&message.body_)};
  if (!message_body)
    return;

  yes_live_snapshot_->write([&](OrderbookStoreSnapshot &store) {
    store.dollars_ = std::move(message_body->yes_dollars_);
    store.start_timestamp_ms_ =
        computeTimeBucket(message_body->timestamp_ms_,
                          constants::CANDLESTICK_HISTORY_GRANULARITY_MS);
  });

  no_live_snapshot_->write([&](OrderbookStoreSnapshot &store) {
    store.dollars_ = std::move(message_body->no_dollars_);
    store.start_timestamp_ms_ =
        computeTimeBucket(message_body->timestamp_ms_,
                          constants::CANDLESTICK_HISTORY_GRANULARITY_MS);
  });

  last_valid_timestamp_ms_.store(message_body->timestamp_ms_,
                                 std::memory_order_release);
  invalid_state_.store(false, std::memory_order_release);

  std::cout << "Applied a patch\n";
}
