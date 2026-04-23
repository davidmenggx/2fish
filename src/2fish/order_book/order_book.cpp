#include "2fish/models/price_change.h"
#include "2fish/models/book_snapshot.h"
#include "2fish/order_book/order_book.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <stdexcept>

void market::OrderBook::applyPriceChange(market::PriceChange& diff) {
	for (const auto& delta : diff.deltas_) {
		if (delta.side_ == market::Side::kBuy) {
			bids_[delta.price_] = delta.size_;
		}
		else {
			asks_[delta.price_] = delta.size_;
		}

		best_bid_ = delta.best_bid_;
		best_ask_ = delta.best_ask_;

		// TODO: verify that the top of book is as expected
	}

	last_message_ = diff.timestamp_;

	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	last_updated_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void market::OrderBook::applySnapshot(market::BookSnapshot& snapshot) {
	bids_ = snapshot.bids_;
	asks_ = snapshot.asks_;

	// when a snapshot occurs, the entire book is changed
	// to match. so we the best bid and ask are reset as well
    best_bid_ = 0;
    best_ask_ = 100;

	for (int i{ 100 }; i >= 0; --i) {
        if (bids_[i] != 0.0) {
            best_bid_ = i;
            break;
        }
    }

	for (int i{ 0 }; i < 101; ++i) {
        if (asks_[i] != 0.0) {
            best_ask_ = i;
            break;
        }
    }

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
