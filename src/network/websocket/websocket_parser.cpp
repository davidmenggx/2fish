#include "common/websocket_data_types.hpp"
#include "websocket_parser.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <boost/asio/buffer.hpp>

WebsocketParser::WebsocketParser(
    moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue) 
    : websocket_queue_{websocket_queue} {}

void WebsocketParser::parseAndPush(boost::asio::mutable_buffer buffer) {

}
