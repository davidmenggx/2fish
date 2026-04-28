#pragma once

#include "2fish/models/market_accumulation.h"
#include "2fish/models/market_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/order_book/order_book.h"
#include "2fish/parser/parser_buffer_pool.h"
#include "2fish/utils/triple_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace market {
	class Engine {
	public:
		// TODO: make sure this supports multiple order books, or at least validate the one order book
		Engine(moodycamel::ReaderWriterQueue<MarketAccumulation*>& engine_queue, 
			parser::ParserBufferPool& parser_buffer_pool,
			TripleBuffer<MarketSnapshot>& market_snapshot_buffer, 
			std::atomic<bool>& running, std::string target_asset_id_raw);

		void start();

		void publishSnapshot();

	private:
		void run();

		void applyUpdates(market::MarketAccumulation* accumulation);

		OrderBook book_{};

		parser::ParserBufferPool& parser_buffer_pool_;
		moodycamel::ReaderWriterQueue<MarketAccumulation*>& engine_queue_;

		TripleBuffer<MarketSnapshot>& market_snapshot_buffer_;

		std::atomic<bool>& running_;

		uint64_t last_message_{}; // milliseconds

		std::string target_asset_id_{};

		std::jthread thread_{};
	};
}
