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

	private:
		// Instead of traditional tree based approaches for the order book, i use a simple
		// array because the only possible price levels are between $0 and $1 (100 cents)
		std::array<long double, 101> bids_{};
		std::array<long double, 101> asks_{};

		uint64_t last_message_{}; // milliseconds
		uint64_t last_updated_{}; // milliseconds
	};
}
