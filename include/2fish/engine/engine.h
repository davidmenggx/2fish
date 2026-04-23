#pragma once

#include "2fish/dispatcher/dispatcher.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/network/websocket_client.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <string_view>

namespace market {
	class Engine {
	public:
		Engine() = delete;

		Engine(std::string target_asset_id_raw);

		void start();

	private:
		NetworkBufferPool buffer_pool_{};
		moodycamel::ReaderWriterQueue<MessageBuffer*> market_queue_{};

		std::atomic<bool> running_{ false };

		std::string target_asset_id_raw_{};

		WebsocketClient client_;
		Dispatcher dispatcher_;
	};
}
