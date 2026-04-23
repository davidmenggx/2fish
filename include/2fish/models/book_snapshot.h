#pragma once

#include <array>
#include <cstdint>

namespace market {
	struct BookSnapshot {
		std::array<long double, 101> bids_{};
		std::array<long double, 101> asks_{};
		uint64_t timestamp_{}; // milliseconds
	};
}
