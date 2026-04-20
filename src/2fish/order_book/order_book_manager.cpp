#include "2fish/order_book/order_book_manager.h"

#include <simdjson.h>

market::OrderBookManager::OrderBookManager(moodycamel::ReaderWriterQueue<MessageBuffer>& market_queue)
	: market_queue_{ market_queue }
{
}

void market::OrderBookManager::parseAndApplyUpdate(simdjson::padded_string_view) {
}
