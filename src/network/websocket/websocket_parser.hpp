#pragma once

#include "common/websocket_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <boost/asio/buffer.hpp>

class WebsocketParser {
public:
  WebsocketParser(
      moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue);

  void parseAndPush(boost::asio::mutable_buffer buffer);

private:
  simdjson::ondemand::parser parser_{};
  moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue_;
};
