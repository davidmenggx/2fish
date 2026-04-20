#pragma once

#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_map>

namespace market {
	class OrderBookManager {
	public:
		// TODO: make sure this knows about the asset id and can populate the books table
		OrderBookManager(moodycamel::ReaderWriterQueue<MessageBuffer>& market_queue);

		// TODO: some type of run function that tries to read from the spsc and convert
		// it to a simdjson padded_sv

		void parseAndApplyUpdate(simdjson::padded_string_view);

	private:
		// maps the asset id to the order book
		std::unordered_map<uint64_t, OrderBook> books_{};
		moodycamel::ReaderWriterQueue<MessageBuffer>& market_queue_;

		std::atomic<bool> running_{};

		std::jthread thread_{};
	};
}
