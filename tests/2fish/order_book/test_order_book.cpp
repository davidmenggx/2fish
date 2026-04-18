#include "2fish/order_book/order_book.h"

#include <gtest/gtest.h>

TEST(OrderBookTest, ExampleTest) {
	market::OrderBook book{};
	EXPECT_EQ(book.test, 1);
}
