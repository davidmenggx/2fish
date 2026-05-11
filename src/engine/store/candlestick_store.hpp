#pragma once

#include "common/containers/swmr_map.hpp"
#include "common/core/websocket_data_types.hpp"
#include "constants.hpp"

#include <cstdint>
#include <memory>

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

  [[nodiscard]] bool recordTradeMessage(WebsocketMessage &message);

  void tryRolloverYesCandlestick(int64_t now_ms);
  void tryRolloverNoCandlestick(int64_t now_ms);

private:
  void clearLiveYesCandlestick();
  void clearLiveNoCandlestick();
  void updateLiveCandlestick(const TradeMessage *message_body);

  // Market yes side
  // CRITICAL MAJOR TODO: THE LIVE IS NOT THREAD SAFE
  std::unique_ptr<CandlestickStoreSnapshot> yes_live_candlestick_{nullptr};
  std::unique_ptr<SwmrMap<int64_t, CandlestickStoreSnapshot,
                          constants::CANDLESTICK_HISTORY_STEPS>>
      yes_map_{nullptr};

  // Market no side
  std::unique_ptr<CandlestickStoreSnapshot> no_live_candlestick_{nullptr};
  std::unique_ptr<SwmrMap<int64_t, CandlestickStoreSnapshot,
                          constants::CANDLESTICK_HISTORY_STEPS>>
      no_map_{nullptr};

  uint64_t last_message_seq_{};
};
