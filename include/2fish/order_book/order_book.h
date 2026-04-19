#pragma once

#include "2fish/models/order_book_diff.h"
#include "2fish/models/order_book_snapshot.h"

#include <array>
#include <cstdint>

namespace market {
	class OrderBook {
	public:
		explicit OrderBook() = default;

		void applySnapshot(OrderBookSnapshot snapshot);

		void applyDiff(OrderBookDiff diff);

		uint64_t getSize(Side side, int price);

	private:
		// Instead of traditional tree based approaches for the order book, i use a simple
		// array because the only possible price levels are between $0 and $1 (100 cents)
		std::array<uint64_t, 101> bids_{};
		std::array<uint64_t, 101> asks_{};

		uint64_t last_message_{}; // milliseconds
		uint64_t last_updated_{}; // milliseconds
	};
}
