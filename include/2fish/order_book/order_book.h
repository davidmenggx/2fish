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

		uint64_t getSize(Side side, int price);

		// DEBUG FOR NOW: --------------------------------

		int getBestBid() const { return best_bid_; }
		int getBestAsk() const { return best_ask_; }

		long double getBestBidSize() const { return bids_[best_bid_]; }
		long double getBestAskSize() const { return asks_[best_ask_]; }

		uint64_t getLastMessageTimestamp() const { return last_message_; }
		uint64_t getLastUpdateTimestamp() const { return last_updated_; }

	private:
		// Instead of traditional tree based approaches for the order book, i use a simple
		// array because the only possible price levels are between $0 and $1 (100 cents)
		std::array<long double, 101> bids_{};
		std::array<long double, 101> asks_{};

		uint64_t last_message_{}; // milliseconds
		uint64_t last_updated_{}; // milliseconds

		int best_bid_{};
		int best_ask_{};
	};
}
