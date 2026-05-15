#pragma once

#include <cstdint>
#include <new>

namespace constants {
// Market data granularity
inline constexpr std::size_t ORDERBOOK_HISTORY_GRANULARITY_MS{1'000};
inline constexpr std::size_t CANDLESTICK_HISTORY_GRANULARITY_MS{60'000};

// Market data capacity
inline constexpr std::size_t ORDERBOOK_HISTORY_STEPS{262'144};
inline constexpr std::size_t CANDLESTICK_HISTORY_STEPS{512};

// Engine settings
inline constexpr uint64_t ENGINE_DEAD_SPIN{2'048};

// Network settings
inline constexpr std::size_t REST_THREAD_COUNT{16};
inline constexpr int64_t REST_MESSAGE_COOLDOWN_MS{2'500};
inline constexpr std::size_t PAST_MESSAGE_LOOKUP_SIZE{1'024};
inline constexpr std::size_t HTTPS_SESSION_POOL_SIZE{64};

// Other
inline constexpr std::size_t CACHE_LINE_SIZE{
    std::hardware_destructive_interference_size};
} // namespace constants

static_assert((constants::ENGINE_DEAD_SPIN &
               (constants::ENGINE_DEAD_SPIN - 1)) == 0,
              "Time interval heartbeat for engine must be a power of 2");
