#pragma once

#include "common/core/rest_data_types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "config.hpp"
#include "network/rest/rest_client.hpp"
#include "store/candlestick_store.hpp"
#include "store/orderbook_store.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <cstdint>
#include <thread>

class Engine {
public:
  Engine(moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
         moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue,
         Config config, std::atomic<bool> &running);

  void start();

private:
  void run();

  void handleOrderbookMismatch();

  const Config config_;

  moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue_;
  moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue_;

  OrderbookStore orderbook_store_{};
  CandlestickStore candlestick_store_{};

  RestClient rest_client_;
  int64_t last_orderbook_mismatch_request_{};

  std::atomic<bool> &running_;
  std::jthread thread_;
};
