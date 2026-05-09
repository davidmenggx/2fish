#pragma once

#include "common/core/websocket_data_types.hpp"
#include "config.hpp"
#include "websocket_parser.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <thread>

class WebsocketClient {
public:
  WebsocketClient(
      moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
      Config config, std::atomic<bool> &running);

  void start();

private:
  void run();

  const Config config_;

  WebsocketParser parser_;

  std::atomic<bool> &running_;
  std::jthread thread_;
};
