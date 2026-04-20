#pragma once

#include "2fish/network/network_buffer_pool.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <thread>

namespace market {
	class WebsocketClient {
	public:
		WebsocketClient(NetworkBufferPool& buffer_pool, 
			moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue,
			std::atomic<bool>& running);

		void start();

	private:
		void run();

		NetworkBufferPool& buffer_pool_;

		moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue_;

		std::atomic<bool>& running_;

		std::jthread thread_;
	};
}
