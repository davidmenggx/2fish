#pragma once

#include <array>
#include <cstdint>

struct OrderbookSnapshot {
	std::array<double, 101> bids_{};
	std::array<double, 101> asks_{};

	int64_t timestamp_{}; // TODO: think about how to populate you

	float getLiquidity(std::size_t level) const { return static_cast<float>(bids_[level] + asks_[level]); }
};
