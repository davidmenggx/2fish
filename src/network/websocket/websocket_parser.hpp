#pragma once

#include "common/websocket_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

class WebsocketParser {
public:
  WebsocketParser(
      moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue);

  void parseAndPush(simdjson::padded_string_view padded_json);

private:
  simdjson::ondemand::parser parser_{};
  moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue_;
};
