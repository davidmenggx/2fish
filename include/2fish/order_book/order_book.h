#pragma once

#include "2fish/models/book_snapshot.h"
#include "2fish/models/price_change.h"

#include <array>
#include <cstdint>

namespace market {
	class OrderBook {
	public:
		explicit OrderBook() = default;

		void applySnapshot(BookSnapshot& snapshot);

		void applyPriceChange(PriceChange& diff);

		// TODO: perhaps remove this? we are only using this for unit tests rn
		uint64_t getSize(Side side, int price);

		const std::array<long double, 101>& getBids() const { return bids_; }
		const std::array<long double, 101>& getAsks() const { return asks_; }

	private:
		// Instead of traditional tree based approaches for the order book, i use a simple
		// array because the only possible price levels are between $0 and $1 (100 cents)
		std::array<long double, 101> bids_{};
		std::array<long double, 101> asks_{};

		int best_bid_{};
		int best_ask_{};
	};
}
