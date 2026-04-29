#pragma once

#include <cstddef>
#include <cstdint>

namespace constants {
	// The number of history levels on the screen at once
	static constexpr std::size_t HISTORY_STEPS{ 256U };
	static constexpr std::size_t PRICE_LEVELS{ 101U };

	static constexpr int64_t TIME_PER_HISTORY_LEVEL{ 100 }; // milliseconds
}
