#pragma once

#include "2fish/dispatcher/dispatcher.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/network/websocket_client.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>

namespace market {
	class Engine {
	public:
		Engine();

		void start();

	private:
		NetworkBufferPool buffer_pool_{};
		moodycamel::ReaderWriterQueue<MessageBuffer*> market_queue_{};

		std::atomic<bool> running_{ false };

		WebsocketClient client_;
		Dispatcher market_engine_;
	};
}
