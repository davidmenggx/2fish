#pragma once

#include "2fish/models/book_snapshot.h"
#include "2fish/models/event_type.h"
#include "2fish/models/price_change.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace market {
	class OrderBookManager {
	public:
		// TODO: make sure this supports multiple order books, or at least validate the one order book
		OrderBookManager(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue,
			NetworkBufferPool& buffer_pool, std::atomic<bool>& running);

		void start();

	private:
		void run();

		void parseAndApplyUpdate(MessageBuffer* message);

		// maps the asset id to the order book
		std::unordered_map<uint64_t, OrderBook> books_{};
		moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue_;

		market::NetworkBufferPool& buffer_pool_;

		simdjson::ondemand::parser parser_{};

		std::atomic<bool>& running_;

		// own and reuse the following objects to send to the order book without
		// excessive allocations
		market::EventType event_type_{};
		market::BookSnapshot book_snapshot_buffer_{};
		market::PriceChange price_change_buffer_{};

		std::jthread thread_{};
	};
}
