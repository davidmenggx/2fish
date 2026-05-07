#include "engine.hpp"
#include "common/websocket_data_types.hpp"
#include "config.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <iostream>
#include <thread>

Engine::Engine(moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
               Config config, std::atomic<bool> &running)
    : websocket_queue_{websocket_queue}, config_{config}, running_{running} {}

void Engine::start() { thread_ = std::jthread(&Engine::run, this); }

void Engine::run() {
  WebsocketMessage websocket_message;
  while (running_.load(std::memory_order_relaxed)) {
    if (!websocket_queue_.try_dequeue(websocket_message)) {
      // spin wait
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
      _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
      __asm__ volatile("yield" ::: "memory");
#endif
      continue;
    }

    switch (websocket_message.message_type_) {
    case WebsocketMessage::MessageType::OrderbookSnapshot:
      std::cout << "Got an orderbook snapshot\n";
      break;
    case WebsocketMessage::MessageType::OrderbookDelta:
      std::cout << "Got an orderbook delta\n";
      break;
    case WebsocketMessage::MessageType::Trade:
      std::cout << "Got a trade\n";
      break;
    case WebsocketMessage::MessageType::Unknown:
      std::cout << "Got an unknown message type\n";
      break;
    }
  }
}
