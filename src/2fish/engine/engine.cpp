#include "2fish/engine/engine.h"
#include "2fish/models/market_accumulation.h"
#include "2fish/models/market_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/models/types.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book.h"
#include "2fish/parser/parser_buffer_pool.h"
#include "2fish/utils/triple_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <format>
#include <iostream>
#include <string>

#include <intrin0.inl.h>

market::Engine::Engine(moodycamel::ReaderWriterQueue<MarketAccumulation*>& engine_queue,
	parser::ParserBufferPool& parser_buffer_pool,
	TripleBuffer<MarketSnapshot>& market_snapshot_buffer,
	std::atomic<bool>& running, std::string target_asset_id_)
	: engine_queue_{ engine_queue }, parser_buffer_pool_{ parser_buffer_pool }
	, market_snapshot_buffer_{ market_snapshot_buffer }
	, running_{ running }, target_asset_id_{ target_asset_id_ }
{
}

void market::Engine::start() {
	thread_ = std::jthread(&market::Engine::run, this);
}

void market::Engine::run() {
	try {
		market::MarketAccumulation* accumulation{};
		int messages_since_write{};

		while (running_) {
			if (!engine_queue_.try_dequeue(accumulation)) {
				if (messages_since_write > 0) {
					publishSnapshot();
					messages_since_write = 0;
				}

				_mm_pause(); // spin wait
				continue;
			}

			applyUpdates(accumulation);

			parser_buffer_pool_.release(accumulation);

			++messages_since_write;

			if (messages_since_write > 100) {
				// if there is a big backlog of messages (high market activity),
				// just force a write periodically (every 100 messages) so the GUI stays live
				publishSnapshot();
				messages_since_write = 0;
			}
		}

		std::cout << "Stop message received, engine stopping\n";
	}
	catch (const std::exception& e) {
		std::cerr << std::format("CRITICAL: Unexpected error in engine: {}\n", e.what());
		throw;
	}
}

void market::Engine::publishSnapshot() {
	MarketSnapshot* buffer{ market_snapshot_buffer_.getWriterBuffer() };

	std::array<double, 101> book_bids{ book_.getBids() };
	std::copy(book_bids.begin(), book_bids.end(), buffer->bids_.begin());

	std::array<double, 101> book_asks{ book_.getAsks() };
	std::copy(book_asks.begin(), book_asks.end(), buffer->asks_.begin());

	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	buffer->last_updated_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

	buffer->last_message_ = last_message_;

	// save our work: switch out the triple buffer
	market_snapshot_buffer_.publishWriterBuffer();
}

void market::Engine::applyUpdates(market::MarketAccumulation* accumulation) {
	if (accumulation->asset_id_ != target_asset_id_) {
		std::cerr << "Unknown asset id\n";
		return;
	}

	switch (accumulation->type_) {
	case EventType::UNKNOWN:
		std::cerr << "Failed to find event_type in json payload, dropping message\n";
		return;
	case EventType::BOOK_SNAPSHOT:
		book_.applySnapshot(accumulation->snapshot_bids_, accumulation->snapshot_asks_);
		break;
	case EventType::PRICE_CHANGE:
		book_.applyPriceChange(accumulation->price_change_deltas_);
		break;
	}
}
