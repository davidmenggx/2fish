#pragma once

#include "2fish/order_book/order_book.h"

#include <cstdint>
#include <unordered_map>

namespace market {
	class BookManager {
	public:
		// TODO
	private:
		std::unordered_map<uint64_t, OrderBook> books_{};
	};
}
