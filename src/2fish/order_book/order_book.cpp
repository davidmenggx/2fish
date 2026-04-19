#include "2fish/models/order_book_diff.h"
#include "2fish/models/order_book_snapshot.h"
#include "2fish/order_book/order_book.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

void market::OrderBook::applyDiff(market::OrderBookDiff diff) {
	for (const auto& delta : diff.deltas_) {
		if (delta.side_ == market::Side::kBuy) {
			bids_[delta.price_] = delta.size_;
		}
		else {
			asks_[delta.price_] = delta.size_;
		}

		// TODO: verify that the top of book is as expected
	}

	last_message_ = diff.timestamp_;

	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	last_updated_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void market::OrderBook::applySnapshot(market::OrderBookSnapshot snapshot) {
	bids_ = std::move(snapshot.bids_);
	asks_ = std::move(snapshot.asks_);

	last_message_ = snapshot.timestamp_;

	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	last_updated_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

uint64_t market::OrderBook::getSize(market::Side side, int price) {
	if (price < 0 || price > 100) {
		throw std::invalid_argument(std::format("Invalid price: {}", price));
	}

	if (side == market::Side::kBuy) {
		return bids_[price];
	}
	else {
		return asks_[price];
	}
}
