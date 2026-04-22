#pragma once

#include <array>
#include <cstdint>

namespace market {
	struct BookSnapshot {
		uint64_t asset_id_{};
		std::array<uint64_t, 101> bids_{};
		std::array<uint64_t, 101> asks_{};
		uint64_t timestamp_{}; // milliseconds
	};
}
