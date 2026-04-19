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

TEST(OrderBookTest, TestInvalidSnapshot) {
	market::OrderBook book{};

	market::OrderBookSnapshot snap{};
	book.applySnapshot(snap);

	EXPECT_THROW(book.getSize(market::Side::kBuy, -1), std::invalid_argument);
	EXPECT_THROW(book.getSize(market::Side::kBuy, 101), std::invalid_argument);
}
