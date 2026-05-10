#include "engine.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/utils/cpu_relax.hpp"
#include "config.hpp"
#include "constants.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <thread>

Engine::Engine(moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
               Config config, std::atomic<bool> &running)
    : websocket_queue_{websocket_queue}, config_{config}, running_{running} {}

void Engine::start() { thread_ = std::jthread(&Engine::run, this); }

void Engine::run() {
  WebsocketMessage websocket_message;
  uint64_t empty_spin_count{0};
  while (running_.load(std::memory_order_relaxed)) {
    if (!websocket_queue_.try_dequeue(websocket_message)) {
      ++empty_spin_count;

      // In case the market goes silent, periodically force a write on the
      // engine to make sure that the candlesticks stay up to date.
      // This is probably less painful than forcing the renderer to impute
      // the data, which could break for the different widgets.
      if ((empty_spin_count & (constants::ENGINE_DEAD_SPIN - 1)) == 0) {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch())
                          .count();
        candlestick_store_.tryRolloverYesCandlestick(now_ms);
        candlestick_store_.tryRolloverNoCandlestick(now_ms);
      }

      cpuRelax();
      continue;
    }

    std::cout << "Got something\n";

    switch (websocket_message.message_type_) {
    case WebsocketMessage::MessageType::Unknown:
      std::cout << "Got an unknown message type\n";
      break;
    case WebsocketMessage::MessageType::Trade:
      if (!candlestick_store_.recordTradeMessage(websocket_message)) {
        std::cerr << "Sequence ID mismatch (trades)! Triggering re-fetch\n";
        // TODO: Handle sequence id mismatch
      }
      break;
    case WebsocketMessage::MessageType::OrderbookSnapshot:
      [[fallthrough]];
    case WebsocketMessage::MessageType::OrderbookDelta:
      if (!orderbook_store_.recordOrderbookMessage(websocket_message)) {
        std::cerr << "Sequence ID mismatch (orderbook)! Triggering re-fetch\n";
        // TODO: Handle sequence id mismatch
      }
      break;
    }

    empty_spin_count = 0;
  }
}
