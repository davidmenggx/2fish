#pragma once

#include <array>
#include <cstdint>

struct MarketSnapshot {
	std::array<long double, 101> bids_{};
	std::array<long double, 101> asks_{};

	uint64_t last_message_{}; // milliseconds
	uint64_t last_updated_{}; // milliseconds
};
