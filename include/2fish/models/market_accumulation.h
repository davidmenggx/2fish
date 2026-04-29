#pragma once

#include "2fish/models/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace market {
	struct OrderLevelDelta {
		int price_{}; // cents
		double size_{};
		Side side_{};
		int best_bid_{}; // cents
		int best_ask_{}; // cents
	};

	struct MarketAccumulation {
		std::string asset_id_{};
		EventType type_{};
		int64_t timestamp_{};

		// fields for event_type book
		std::array<double, 101> snapshot_bids_{};
		std::array<double, 101> snapshot_asks_{};

		// fields for event_type price_change
		std::vector<OrderLevelDelta> price_change_deltas_{};
	};
}
