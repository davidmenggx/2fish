#pragma once

#include "common/websocket_data_types.hpp"
#include "config.h"
#include "engine/engine.hpp"
#include "network/websocket/websocket_client.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>

class Driver {
public:
  Driver(Config config);

  void start();

private:
  moodycamel::ReaderWriterQueue<WebsocketMessage> websocket_queue_{};

  std::atomic<bool> running_{ false };

  Engine engine_;
  WebsocketClient websocket_client_;
};
