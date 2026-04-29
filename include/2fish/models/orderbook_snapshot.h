#pragma once

#include <array>
#include <cstdint>

struct OrderbookSnapshot {
	std::array<double, 101> bids_{};
	std::array<double, 101> asks_{};

	int64_t timestamp_{};
};
