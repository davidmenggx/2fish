#pragma once

#include "types.hpp"
#include "constants.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

struct OrderbookSnapshotMessageWs {
  // The maximum length of Kalshi IDs is 200 characters
  std::array<char, 200> market_ticker_{};
  std::array<char, 200> market_id_{};
  std::array<long double, 101> yes_dollars_{};
  std::array<long double, 101> no_dollars_{};

  bool operator==(const OrderbookSnapshotMessageWs &) const = default;

  void set_market_ticker(std::string_view ticker) {
    market_ticker_.fill('\0'); // Ensure null-termination
    auto len = std::min(ticker.size(), constants::MAX_ID_LENGTH_CHARS - 1);
    std::copy_n(ticker.begin(), len, market_ticker_.begin());
  }

  void set_market_id(std::string_view market_id) {
    market_id_.fill('\0'); // Ensure null-termination
    auto len = std::min(market_id.size(), constants::MAX_ID_LENGTH_CHARS - 1);
    std::copy_n(market_id.begin(), len, market_id_.begin());
  }
};

struct OrderbookDeltaMessageWs {
  // The maximum length of Kalshi IDs is 200 characters
  std::array<char, 200> market_ticker_{};
  std::array<char, 200> market_id_{};
  int8_t price_cents_{};
  long double delta_{};
  Side side_{Side::Unknown};
  int64_t timestamp_ms_{};

  bool operator==(const OrderbookDeltaMessageWs &) const = default;

  void set_market_ticker(std::string_view ticker) {
    market_ticker_.fill('\0'); // Ensure null-termination
    auto len = std::min(ticker.size(), constants::MAX_ID_LENGTH_CHARS - 1);
    std::copy_n(ticker.begin(), len, market_ticker_.begin());
  }

  void set_market_id(std::string_view market_id) {
    market_id_.fill('\0'); // Ensure null-termination
    auto len = std::min(market_id.size(), constants::MAX_ID_LENGTH_CHARS - 1);
    std::copy_n(market_id.begin(), len, market_id_.begin());
  }
};

struct TradeMessageWs {
  // The maximum length of Kalshi IDs is 200 characters
  std::array<char, 200> market_ticker_{};
  std::array<char, 200> trade_id_{};
  uint8_t yes_price_cents_{};
  uint8_t no_price_cents_{};
  double contracts_traded_{}; // The "count_fp" field
  Side taker_side_{Side::Unknown};
  int64_t timestamp_ms_{};

  bool operator==(const TradeMessageWs &) const = default;

  void set_market_ticker(std::string_view ticker) {
    market_ticker_.fill('\0'); // Ensure null-termination
    auto len = std::min(ticker.size(), constants::MAX_ID_LENGTH_CHARS - 1);
    std::copy_n(ticker.begin(), len, market_ticker_.begin());
  }

  void set_trade_id(std::string_view trade_id) {
    trade_id_.fill('\0'); // Ensure null-termination
    auto len = std::min(trade_id.size(), constants::MAX_ID_LENGTH_CHARS - 1);
    std::copy_n(trade_id.begin(), len, trade_id_.begin());
  }
};

struct WebsocketMessage {
  enum class MessageType { OrderbookSnapshot, OrderbookDelta, Trade, Unknown };

  MessageType message_type_{MessageType::Unknown};
  uint64_t sequence_id_{}; // Valid sequence IDs are >= 1

  std::variant<OrderbookSnapshotMessageWs, OrderbookDeltaMessageWs,
               TradeMessageWs>
      body_;
};

inline WebsocketMessage::MessageType
getWebsocketMessageType(std::string_view raw_type) {
  if (raw_type == "orderbook_snapshot")
    return WebsocketMessage::MessageType::OrderbookSnapshot;
  else if (raw_type == "orderbook_delta")
    return WebsocketMessage::MessageType::OrderbookDelta;
  else if (raw_type == "trade")
    return WebsocketMessage::MessageType::Trade;
  else
    return WebsocketMessage::MessageType::Unknown;
}
