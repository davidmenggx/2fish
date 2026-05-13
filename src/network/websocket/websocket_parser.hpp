#pragma once

#include "common/core/websocket_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

class WebsocketParser {
public:
  WebsocketParser(
      moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue);

  void parseAndPush(simdjson::padded_string_view padded_json);

private:
  OrderbookSnapshotMessageWs
  parseOrderbookSnapshot(simdjson::ondemand::object &msg);
  OrderbookDeltaMessageWs parseOrderbookDelta(simdjson::ondemand::object &msg);
  TradeMessageWs parseTrade(simdjson::ondemand::object &msg);

  simdjson::ondemand::parser parser_{};
  moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue_;
};
