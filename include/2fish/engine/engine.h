#pragma once

#include "moodycamel/readerwriterqueue.h"

#include "2fish/network/websocket_client.h"
#include "2fish/order_book/book_manager.h"
#include "2fish/parser/parser.h"

namespace market {
	class Engine {
	public:
		// TODO
	private:
		WebsocketClient client_{};
		Parser parser_{};
		BookManager books_{};

		moodycamel::ReaderWriterQueue parser_queue_{};
		moodycamel::ReaderWriterQueue book_queue_{};
	};
}
