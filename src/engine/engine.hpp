#pragma once

#include "common/websocket_data_types.hpp"
#include "config.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <thread>

class Engine {
public:
  Engine(moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
         Config config, std::atomic<bool> &running);

  void start();

private:
  void run();

  const Config config_;

  moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue_;

  std::atomic<bool> &running_;
  std::jthread thread_;
};
