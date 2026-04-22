#pragma once

#include "2fish/network/network_buffer_pool.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <thread>

namespace market {
	class WebsocketClient {
	public:
		WebsocketClient(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue,
			NetworkBufferPool& buffer_pool, std::atomic<bool>& running);

		void start();

	private:
		void run();

		moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue_;

		NetworkBufferPool& buffer_pool_;

		std::atomic<bool>& running_;

		std::jthread thread_;
	};
}
