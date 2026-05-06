#pragma once

#include <atomic>
#include <thread>

class WebsocketClient {
public:
  WebsocketClient(std::atomic<bool> &running);

  void start();

private:
  void run();

  std::atomic<bool>& running_;
  std::jthread thread_;
};
