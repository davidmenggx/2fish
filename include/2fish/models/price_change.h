#pragma once

#include <cstdint>
#include <vector>

namespace market {
	enum class Side{ kBuy, kSell };

	struct OrderLevelDelta {
		int price_{}; // cents
		long double size_{};
		Side side_{};
		int best_bid_{}; // cents
		int best_ask_{}; // cents
	};

	struct PriceChange {
		std::vector<OrderLevelDelta> deltas_{};
	};
}
