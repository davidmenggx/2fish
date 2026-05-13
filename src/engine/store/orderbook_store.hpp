#pragma once

#include "common/containers/ring_buffer.hpp"
#include "common/containers/seqlock_wrapper.hpp"
#include "common/core/rest_data_types.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

struct OrderbookStoreSnapshot {
  std::array<long double, 101> dollars_{};
  int64_t start_timestamp_ms_{};
};

class OrderbookStore {
public:
  OrderbookStore();

  [[nodiscard]] bool recordOrderbookMessage(WebsocketMessage &message);

  // Called by the renderer clients to fetch the data at a specific timestamp,
  // or none if it does not exist.
  [[nodiscard]] std::optional<OrderbookStoreSnapshot>
  get(int64_t query_timestamp_ms, Side side);

  // Repair internal data state in the event of a sequence ID mismatch.
  void tryPatch(RestMessage &message);

  // TODO^^^^: MAKE SURE THAT HOWEVER WE ARE PATCHING, WE MAKE SURE THAT ONCE
  // WE RESUME THE TIMESTAMPS LINE UP

private:
  void recordOrderbookDelta(WebsocketMessage &message);
  void recordOrderbookSnapshot(WebsocketMessage &message);

  // Market yes side
  std::unique_ptr<SeqLockWrapper<OrderbookStoreSnapshot>> yes_live_snapshot_{
      nullptr};
  std::unique_ptr<
      RingBuffer<OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>
      yes_buffer_{nullptr};

  // Market no side
  std::unique_ptr<SeqLockWrapper<OrderbookStoreSnapshot>> no_live_snapshot_{
      nullptr};
  std::unique_ptr<
      RingBuffer<OrderbookStoreSnapshot, constants::ORDERBOOK_HISTORY_STEPS>>
      no_buffer_{nullptr};

  uint64_t last_message_seq_{};

  // Internal state validation
  std::atomic<bool> invalid_state_{false};
  std::atomic<int64_t> last_valid_timestamp_ms_{};
  std::atomic<bool> state_patched_{false};
};
