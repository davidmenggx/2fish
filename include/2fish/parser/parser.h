#pragma once

#include "2fish/models/market_accumulation.h"
#include "2fish/models/trade.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/parser/parser_buffer_pool.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <atomic>
#include <string>

namespace parser {
	enum class ParserReturnCode {
		SUCCESS,
		PARSE_ERROR,
		SEMANTIC_ERROR,
	};

	class Parser {
	public:
		Parser(moodycamel::ReaderWriterQueue<market::MessageBuffer*>& network_queue, 
			moodycamel::ReaderWriterQueue<market::MarketAccumulation*>& engine_queue,
			moodycamel::ReaderWriterQueue<market::Trade>& trade_queue, 
			market::NetworkBufferPool& network_buffer_pool, parser::ParserBufferPool& parser_buffer_pool,
			std::atomic<bool>& running, std::string target_asset_id);

		void start();

	private:
		void run();

		[[nodiscard]] ParserReturnCode parseDataToBuffer(market::MessageBuffer* message, market::MarketAccumulation* buffer);

		// Parsing helpers:
		void getBids(simdjson::ondemand::array bids_arr, market::MarketAccumulation* buffer);
		void getAsks(simdjson::ondemand::array asks_arr, market::MarketAccumulation* buffer);
		void getPriceChanges(simdjson::ondemand::array price_change_arr, market::MarketAccumulation* buffer);

		moodycamel::ReaderWriterQueue<market::MessageBuffer*>& network_queue_;
		moodycamel::ReaderWriterQueue<market::MarketAccumulation*>& engine_queue_;
		moodycamel::ReaderWriterQueue<market::Trade>& trade_queue_;

		market::NetworkBufferPool& network_buffer_pool_;
		parser::ParserBufferPool& parser_buffer_pool_;

		simdjson::ondemand::parser parser_{};

		std::atomic<bool>& running_;

		market::Trade trade_accumulator_{};

		std::string target_asset_id_{};

		std::jthread thread_{};
	};
}
