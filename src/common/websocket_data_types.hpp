#pragma once

#include "types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <variant>

struct WebsocketMessage {
  enum class MessageType { OrderbookSnapshot, OrderbookDelta, Trade, Unknown };

  MessageType message_type_{MessageType::Unknown};
  uint64_t sequence_id_{};

  std::variant<OrderbookSnapshotMessage, OrderbookDeltaMessage, TradeMessage>
      message_;
};

struct OrderbookSnapshotMessage {
  std::string market_ticker_{};
  std::string market_id_{};
  std::array<long double, 101> yes_dollars_{};
  std::array<long double, 101> no_dollars_{};
};

struct OrderbookDeltaMessage {
  std::string market_ticker_{};
  std::string market_id_{};
  int8_t price_cents_{};
  long double delta_{};
  Side side_{};
  int64_t timestamp_ms_{};
};

struct TradeMessage {
  std::string trade_id_{}; // TODO: Only keep if we are using a live-trade log
  std::string market_ticker_{};
  int8_t yes_price_cents_{};
  int8_t no_price_cents_{};
  double contracts_traded_{}; // The "count_fp" field
  Side taker_side_{};
  int64_t timestamp_ms_{};
};
