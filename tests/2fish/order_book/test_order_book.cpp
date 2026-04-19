#include "2fish/models/order_book_diff.h"
#include "2fish/models/order_book_snapshot.h"
#include "2fish/order_book/order_book.h"

#include <gtest/gtest.h>

#include <stdexcept>

TEST(OrderBookTest, TestValidSnapshot) {
	market::OrderBook book{};

	market::OrderBookSnapshot snap{};
	
	snap.bids_[50] = 1000;
	snap.bids_[25] = 900;
	
	snap.asks_[0] = 800;

	book.applySnapshot(snap);

	EXPECT_EQ(book.getSize(market::Side::kBuy, 50), 1000);
	EXPECT_EQ(book.getSize(market::Side::kBuy, 25), 900);
	EXPECT_EQ(book.getSize(market::Side::kBuy, 67), 0);

	EXPECT_EQ(book.getSize(market::Side::kSell, 0), 800);
	EXPECT_EQ(book.getSize(market::Side::kSell, 67), 0);
}

TEST(OrderBookTest, TestValidDiff) {
	market::OrderBook book{};

	market::OrderBookDiff diff{};

	market::OrderLevelDelta d1{
		.price_ = 34,
		.size_ = 10'000,
		.side_ = market::Side::kBuy,
		.best_bid_ = 34,
		.best_ask_ = 38
	};

	market::OrderLevelDelta d2{
		.price_ = 66,
		.size_ = 50'000,
		.side_ = market::Side::kSell,
		.best_bid_ = 66,
		.best_ask_ = 62
	};

	diff.deltas_.push_back(d1);
	diff.deltas_.push_back(d2);

	book.applyDiff(diff);

	EXPECT_EQ(book.getSize(market::Side::kBuy, 34), 10'000);
	EXPECT_EQ(book.getSize(market::Side::kSell, 66), 50'000);
}

TEST(OrderBookTest, TestInvalidRead) {
	market::OrderBook book{};

	market::OrderBookSnapshot snap{};
	book.applySnapshot(snap);

	EXPECT_THROW(book.getSize(market::Side::kBuy, -1), std::invalid_argument);
	EXPECT_THROW(book.getSize(market::Side::kBuy, 101), std::invalid_argument);
}
