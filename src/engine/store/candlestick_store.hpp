#pragma once

#include "common/containers/seqlock_wrapper.hpp"
#include "common/containers/swmr_map.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/core/rest_data_types.hpp"
#include "constants.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

struct CandlestickStoreSnapshot {
  // 255 is a sentinel value for no data: all valid data must be between 0-100
  uint8_t open_{255};
  uint8_t high_{255};
  uint8_t low_{255};
  uint8_t close_{255};
  int64_t start_timestamp_ms_{};
};

class CandlestickStore {
public:
  CandlestickStore();

  // TODO: A sophisticated fetcher that either:
  // 1) gets the live candlestick
  // 2) finds the historical candlestick, or start a query

  [[nodiscard]] bool recordTradeMessageWs(WebsocketMessage &message);

  // Called by the renderer clients to fetch the data at a specific timestamp,
  // or none if it does not exist.
  [[nodiscard]] std::optional<CandlestickStoreSnapshot>
  get(int64_t query_timestamp_ms, Side side);

  // Repair internal data state in the event of a sequence ID mismatch.
  void tryPatch(RestMessage &message);

  void tryRolloverYesCandlestick(int64_t now_ms);
  void tryRolloverNoCandlestick(int64_t now_ms);

private:
  // Market yes side
  std::unique_ptr<SeqLockWrapper<CandlestickStoreSnapshot>>
      yes_live_candlestick_{nullptr};
  std::unique_ptr<SwmrMap<int64_t, CandlestickStoreSnapshot,
                          constants::CANDLESTICK_HISTORY_STEPS>>
      yes_map_{nullptr};

  // Market no side
  std::unique_ptr<SeqLockWrapper<CandlestickStoreSnapshot>>
      no_live_candlestick_{nullptr};
  std::unique_ptr<SwmrMap<int64_t, CandlestickStoreSnapshot,
                          constants::CANDLESTICK_HISTORY_STEPS>>
      no_map_{nullptr};

  uint64_t last_message_seq_{};

  // Internal state validation
  std::atomic<bool> invalid_state_{false};
  std::atomic<int64_t> last_valid_timestamp_ms_{};
  std::atomic<bool> state_patched_{false};
};
