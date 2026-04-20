#pragma once

#include "2fish/network/network_buffer_pool.h"
#include "2fish/network/websocket_client.h"
#include "2fish/order_book/order_book_manager.h"

#include "moodycamel/readerwriterqueue.h"

namespace market {
	class Engine {
	public:
		// TODO
	private:
		WebsocketClient client_;
		OrderBookManager books_;

		NetworkBufferPool buffer_pool_{};

		moodycamel::ReaderWriterQueue<MessageBuffer> market_queue_{};
	};
}
