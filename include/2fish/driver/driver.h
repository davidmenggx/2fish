#pragma once

#include "2fish/engine/engine.h"
#include "2fish/models/market_snapshot.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/network/websocket_client.h"
#include "2fish/renderer/renderer.h"
#include "2fish/utils/triple_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <string_view>

class Driver {
public:
	Driver() = delete;

	Driver(std::string target_asset_id_raw);

	void start();

private:
	market::NetworkBufferPool buffer_pool_{};
	moodycamel::ReaderWriterQueue<market::MessageBuffer*> market_queue_{};

	TripleBuffer<MarketSnapshot> market_snapshot_buffer_;

	std::atomic<bool> running_{ false };

	std::string target_asset_id_raw_{};

	market::WebsocketClient client_;
	market::Engine engine_;
	renderer::Renderer renderer_;
};
