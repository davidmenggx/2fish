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
inline constexpr std::size_t TRADE_LEDGER_CAPACITY{1'024};

// Engine settings
inline constexpr uint64_t ENGINE_DEAD_SPIN{2'048};

// Network settings
inline constexpr std::size_t REST_THREAD_COUNT{16};
inline constexpr int64_t REST_MESSAGE_COOLDOWN_MS{1'000};
inline constexpr std::size_t PAST_MESSAGE_LOOKUP_SIZE{1'024};
inline constexpr std::size_t HTTPS_SESSION_POOL_SIZE{64};

// Other
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t CACHE_LINE_SIZE =
    std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

// Kalshi specific
inline constexpr std::size_t MAX_ID_LENGTH_CHARS{200};
} // namespace constants

static_assert((constants::ENGINE_DEAD_SPIN &
               (constants::ENGINE_DEAD_SPIN - 1)) == 0,
              "Time interval heartbeat for engine must be a power of 2");
