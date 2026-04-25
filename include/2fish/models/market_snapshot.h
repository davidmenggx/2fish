#pragma once

#include <array>
#include <cstdint>

struct MarketSnapshot {
	std::array<uint8_t, 101> bids_weight_{}; // as a percent
	std::array<uint8_t, 101> asks_weight_{};  // as a percent

	uint64_t last_message_{}; // milliseconds
	uint64_t last_updated_{}; // milliseconds
};
