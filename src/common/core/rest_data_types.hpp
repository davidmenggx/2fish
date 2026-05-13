#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <variant>

struct CandlestickMessageRest {
  // TODO
};

struct OrderbookSnapshotMessageRest {
  std::array<long double, 101> yes_dollars_{};
  std::array<long double, 101> no_dollars_{};
  int64_t timestamp_ms_{std::numeric_limits<int64_t>::min()};
};

struct RestMessage {
  enum class MessageType { OrderbookSnapshot, Candlestick, Unknown };

  MessageType message_type_{MessageType::Unknown};

  std::variant<CandlestickMessageRest, OrderbookSnapshotMessageRest> body_;
};
