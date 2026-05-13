#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

struct CandlestickMessageRest {
  struct Candlestick {
    int64_t end_period_ts_ms_{};
    uint8_t open_cents_{};
    uint8_t high_cents_{};
    uint8_t low_cents_{};
    uint8_t close_cents_{};
  };

  // TODO: Vector here is a bit questionable
  std::vector<Candlestick> candlesticks_{};
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
