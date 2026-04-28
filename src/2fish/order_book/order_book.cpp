#include "2fish/models/market_accumulation.h"
#include "2fish/models/types.h"
#include "2fish/order_book/order_book.h"

#include <array>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vector>

void market::OrderBook::applyPriceChange(std::vector<OrderLevelDelta>& price_change_deltas) {
	for (const auto& delta : price_change_deltas) {
		if (delta.side_ == market::Side::BUY) {
			bids_[delta.price_] = delta.size_;
		}
		else {
			asks_[delta.price_] = delta.size_;
		}

		// TODO: verify that the top of book is as expected
	}
}

void market::OrderBook::applySnapshot(std::array<double, 101>& snapshot_bids,
	std::array<double, 101>& snapshot_asks) {
	bids_ = snapshot_bids;
	asks_ = snapshot_asks;
}

uint64_t market::OrderBook::getSize(market::Side side, int price) {
	if (price < 0 || price > 100) {
		throw std::invalid_argument(std::format("Invalid price: {}", price));
	}

	if (side == market::Side::BUY) {
		return bids_[price];
	}
	else {
		return asks_[price];
	}
}
