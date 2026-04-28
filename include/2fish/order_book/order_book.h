#pragma once

#include "2fish/models/market_accumulation.h"

#include <array>
#include <cstdint>
#include <vector>

namespace market {
	class OrderBook {
	public:
		explicit OrderBook() = default;

		void applySnapshot(std::array<double, 101>& snapshot_bids, std::array<double, 101>& snapshot_asks);

		void applyPriceChange(std::vector<OrderLevelDelta>& price_change_deltas);

		// TODO: perhaps remove this? we are only using this for unit tests rn
		uint64_t getSize(Side side, int price);

		const std::array<double, 101>& getBids() const { return bids_; }
		const std::array<double, 101>& getAsks() const { return asks_; }

	private:
		// Instead of traditional tree based approaches for the order book, i use a simple
		// array because the only possible price levels are between $0 and $1 (100 cents)
		std::array<double, 101> bids_{};
		std::array<double, 101> asks_{};
	};
}
