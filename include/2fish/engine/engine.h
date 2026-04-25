#pragma once

#include "2fish/models/book_snapshot.h"
#include "2fish/models/event_type.h"
#include "2fish/models/market_snapshot.h"
#include "2fish/models/price_change.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book.h"
#include "2fish/utils/triple_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_map>

namespace market {
	class Engine {
	public:
		// TODO: make sure this supports multiple order books, or at least validate the one order book
		Engine(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue, NetworkBufferPool& buffer_pool,
			TripleBuffer<MarketSnapshot>& market_snapshot_buffer, std::atomic<bool>& running, std::string target_asset_id_raw);

		void start();

		void publishSnapshot();

	private:
		void run();

		void parseAndApplyUpdates(MessageBuffer* message);

		OrderBook book_{};

		TripleBuffer<MarketSnapshot>& market_snapshot_buffer_;
		moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue_;

		market::NetworkBufferPool& buffer_pool_;

		simdjson::ondemand::parser parser_{};

		std::atomic<bool>& running_;

		// own and reuse the following objects to send to the order book without
		// excessive allocations: "accumulator pattern"
		market::EventType event_type_accumulator_{};
		std::string current_asset_id_accumulator_{};
		market::BookSnapshot book_snapshot_buffer_accumulator_{};
		market::PriceChange price_change_buffer_accumulator_{};

		uint64_t last_message_{}; // milliseconds

		std::string target_asset_id_raw_{};

		std::jthread thread_{};
	};
}
