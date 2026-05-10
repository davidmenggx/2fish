#pragma once

#include "common/containers/ring_buffer.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct OrderbookStoreSnapshot {
  std::array<long double, 101> dollars_{};
  int64_t start_timestamp_ms_{};
};

class OrderbookStore {
public:
  OrderbookStore();

  [[nodiscard]] bool recordOrderbookMessage(WebsocketMessage &message);

  [[nodiscard]] std::optional<OrderbookStoreSnapshot>
  get(int64_t query_timestamp, Side side);

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
  std::vector<OrderbookStoreSnapshot> yes_fetch_buffer_{};

  // Market no side
  std::unique_ptr<OrderbookStoreSnapshot> no_live_snapshot_{nullptr};
  std::unique_ptr<
      RingBuffer<OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>
      no_buffer_{nullptr};
  std::vector<OrderbookStoreSnapshot> no_fetch_buffer_{};

  uint64_t last_message_seq_{};
  bool invalid_state_{false};
  int64_t last_valid_timestamp_ms_{};
};
