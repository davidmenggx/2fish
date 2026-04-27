#pragma once

#include <array>
#include <cstdint>

namespace market {
	struct BookSnapshot {
		std::array<double, 101> bids_{};
		std::array<double, 101> asks_{};
	};
}
