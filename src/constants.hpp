#pragma once

#include <cstddef>

namespace constants {
// Market data granularity
inline constexpr std::size_t ORDERBOOK_HISTORY_GRANULARITY_MS{1'000};

// Market data capacity
inline constexpr std::size_t ORDERBOOK_HISTORY_STEPS{262'144};
} // namespace constants
