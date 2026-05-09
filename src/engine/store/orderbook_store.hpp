#pragma once

#include "common/containers/ring_buffer.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <array>
#include <cstdint>
#include <memory>

struct OrderbookStoreSnapshot {
  std::array<long double, 101> yes_dollars_{};
  std::array<long double, 101> no_dollars_{};
  int64_t start_timestamp_ms_{};
};

class OrderbookStore {
public:
  OrderbookStore();

  [[nodiscard]] bool recordOrderbookMessage(WebsocketMessage &message);

  [[nodiscard]] int64_t getEarliestMessageTimestampMs() const {
    return earliest_timestamp_ms_;
  }

private:
  void clearLiveSnapshot();

  void recordOrderbookDelta(WebsocketMessage &message);
  void recordOrderbookSnapshot(WebsocketMessage &message);

  std::unique_ptr<OrderbookStoreSnapshot> live_snapshot_{nullptr};
  std::unique_ptr<
      RingBuffer<OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>
      buffer_{nullptr};

  int64_t earliest_timestamp_ms_{};
  uint64_t last_message_seq_{};
};
