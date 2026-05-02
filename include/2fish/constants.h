#pragma once

#include <cstddef>

namespace constants {
	// the number of candlesticks (horizontal) that can be shown on screen at a time
	static constexpr std::size_t HISTORY_STEPS{ 32U };

	static constexpr std::size_t PRICE_LEVELS{ 101U };

	static constexpr std::size_t CANDLESTICK_INTERVAL{ 60'000 }; // ms
	// sadly we cannot have 1 orderbook snapshot per second, as the snapshots per candlestick
	// needs to be a power of 2
	static constexpr std::size_t ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK{ 64 };
	static constexpr std::size_t ORDERBOOK_INTERVAL{ CANDLESTICK_INTERVAL / ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK };

	static constexpr std::size_t WINDOW_DURATION{ HISTORY_STEPS * CANDLESTICK_INTERVAL }; // ms
}
