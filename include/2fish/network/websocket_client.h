#pragma once

#include "2fish/network/network_buffer_pool.h"

#include "moodycamel/readerwriterqueue.h"

namespace market {
	class WebsocketClient {
	public:
		WebsocketClient(NetworkBufferPool& buffer_pool, 
			moodycamel::ReaderWriterQueue<MessageBuffer>& market_queue);

		// TODO: your job is to enqueue messages to the market queue after reading them

	private:
		NetworkBufferPool& buffer_pool_;

		moodycamel::ReaderWriterQueue<MessageBuffer>& market_queue_;
	};
}
