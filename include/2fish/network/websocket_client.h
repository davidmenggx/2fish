#pragma once

#include "2fish/network/network_buffer_pool.h"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <string>
#include <thread>

namespace market {
	class WebsocketClient {
	public:
		WebsocketClient(moodycamel::ReaderWriterQueue<MessageBuffer*>& network_queue,
			NetworkBufferPool& network_buffer_pool, std::atomic<bool>& running,
			std::string target_asset_id_raw);

		void start();

	private:
		void run();

		moodycamel::ReaderWriterQueue<MessageBuffer*>& network_queue_;

		NetworkBufferPool& network_buffer_pool_;

		std::atomic<bool>& running_;

		std::string target_asset_id_raw_{};

		std::jthread thread_;
	};
}
