#pragma once

#include "2fish/engine/engine.h"
#include "2fish/models/market_accumulation.h"
#include "2fish/models/market_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/network/websocket_client.h"
#include "2fish/parser/parser.h"
#include "2fish/parser/parser_buffer_pool.h"
#include "2fish/renderer/renderer.h"
#include "2fish/utils/triple_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <string>
#include <string_view>

class Driver {
public:
	Driver() = delete;

	Driver(std::string target_asset_id);

	void start();

private:
	market::NetworkBufferPool network_buffer_pool_{};
	moodycamel::ReaderWriterQueue<market::MessageBuffer*> network_queue_{};

	parser::ParserBufferPool parser_buffer_pool_{};
	moodycamel::ReaderWriterQueue<market::MarketAccumulation*> engine_queue_{};

	moodycamel::ReaderWriterQueue<market::Trade> trade_queue_{};

	TripleBuffer<MarketSnapshot> market_snapshot_buffer_;

	std::atomic<bool> running_{ false };

	market::WebsocketClient client_;
	parser::Parser parser_;
	market::Engine engine_;
	renderer::Renderer renderer_;
};
