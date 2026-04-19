#pragma once

#include <cstdint>
#include <vector>

namespace market {
	enum class Side{ kBuy, kSell };

	struct OrderLevelDelta {
		int price_{}; // cents
		uint64_t size_{};
		Side side_{};
		int best_bid_{}; // cents
		int best_ask_{}; // cents
	};

	struct OrderBookDiff {
		uint64_t asset_id_{};
		std::vector<OrderLevelDelta> deltas_{};
		uint64_t timestamp_{}; // milliseconds
	};
}
