#pragma once

#include <cstddef>
#include <cstdint>

namespace constants {
// Market data granularity
inline constexpr std::size_t ORDERBOOK_HISTORY_GRANULARITY_MS{1'000};
inline constexpr std::size_t CANDLESTICK_HISTORY_GRANULARITY_MS{1'000};

// Market data capacity
inline constexpr std::size_t ORDERBOOK_HISTORY_STEPS{262'144};
inline constexpr std::size_t CANDLESTICK_HISTORY_STEPS{512};

// Engine settings
inline constexpr uint64_t TIME_INTERVAL_CHECK{2'048};
} // namespace constants
