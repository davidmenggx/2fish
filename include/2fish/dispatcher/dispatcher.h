#pragma once

#include "2fish/models/book_snapshot.h"
#include "2fish/models/event_type.h"
#include "2fish/models/price_change.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_map>

namespace market {
	class Dispatcher {
	public:
		// TODO: make sure this supports multiple order books, or at least validate the one order book
		Dispatcher(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue, NetworkBufferPool& buffer_pool, 
			std::atomic<bool>& running, std::string_view target_asset_id_raw);

		void start();

	private:
		void run();

		void parseAndApplyUpdates(MessageBuffer* message);

		OrderBook book_{};
		moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue_;

		market::NetworkBufferPool& buffer_pool_;

		simdjson::ondemand::parser parser_{};

		std::atomic<bool>& running_;

		// own and reuse the following objects to send to the order book without
		// excessive allocations
		market::EventType event_type_{};
		uint64_t asset_id_{};
		market::BookSnapshot book_snapshot_buffer_{};
		market::PriceChange price_change_buffer_{};

		// for now i am just supporting one order book (asset_id) at a time.
		// but price_change messages can contain multiple asset_ids, usually
		// corresponding to the yes/no markets. to verify we are capturing the
		// correct market, hard match the asset id hash

		std::string_view target_asset_id_raw_{};
		uint64_t target_asset_id_hash_{};

		std::jthread thread_{};
	};
}
