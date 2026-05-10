#pragma once

#include "common/containers/ring_buffer.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>

struct OrderbookStoreSnapshot {
  std::array<long double, 101> dollars_{};
  int64_t start_timestamp_ms_{};
};

class OrderbookStore {
public:
  OrderbookStore();

  [[nodiscard]] bool recordOrderbookMessage(WebsocketMessage &message);

  [[nodiscard]] int64_t getEarliestYesTimestampMs() const {
    return earliest_yes_timestamp_ms_;
  }

  [[nodiscard]] int64_t getEarliestNoTimestampMs() const {
    return earliest_no_timestamp_ms_;
  }

private:
  void clearLiveYesSnapshot();
  void clearLiveNoSnapshot();

  void recordOrderbookDelta(WebsocketMessage &message);
  void recordOrderbookSnapshot(WebsocketMessage &message);

  // Market yes side
  std::unique_ptr<OrderbookStoreSnapshot> yes_live_snapshot_{nullptr};
  std::unique_ptr<
      RingBuffer<OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>
      yes_buffer_{nullptr};

  // Market no side
  std::unique_ptr<OrderbookStoreSnapshot> no_live_snapshot_{nullptr};
  std::unique_ptr<
      RingBuffer<OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>
      no_buffer_{nullptr};

  int64_t earliest_yes_timestamp_ms_{std::numeric_limits<int64_t>::min()};
  int64_t earliest_no_timestamp_ms_{std::numeric_limits<int64_t>::min()};
  uint64_t last_message_seq_{};
};
