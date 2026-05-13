#include "engine.hpp"
#include "common/core/rest_data_types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/utils/cpu_relax.hpp"
#include "config.hpp"
#include "constants.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

Engine::Engine(moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
               moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue,
               Config config, std::atomic<bool> &running)
    : websocket_queue_{websocket_queue}, rest_patch_queue_{rest_patch_queue},
      rest_client_{rest_patch_queue}, config_{config}, running_{running} {}

void Engine::start() { thread_ = std::jthread(&Engine::run, this); }

void Engine::run() {
  WebsocketMessage websocket_message;
  RestMessage rest_message;
  uint64_t empty_spin_count{0};
  while (running_.load(std::memory_order_relaxed)) {
    // Fixing any failed states takes priority over reading new data
    if (rest_patch_queue_.try_dequeue(rest_message)) {
      switch (rest_message.message_type_) {
      case RestMessage::MessageType::OrderbookSnapshot:
        orderbook_store_.tryPatch(rest_message);
        break;
      case RestMessage::MessageType::Candlestick:
        // TODO: Do something candlestick here
        break;
      case RestMessage::MessageType::Unknown:
        break;
      }

      empty_spin_count = 0;
      continue;
    }

    if (websocket_queue_.try_dequeue(websocket_message)) {
      switch (websocket_message.message_type_) {
      case WebsocketMessage::MessageType::Trade:
        if (!candlestick_store_.recordTradeMessageWs(websocket_message)) {
          // TODO: Handle sequence id mismatch
        }
        break;
      case WebsocketMessage::MessageType::OrderbookSnapshot:
        [[fallthrough]];
      case WebsocketMessage::MessageType::OrderbookDelta:
        if (!orderbook_store_.recordOrderbookMessage(websocket_message)) {
          handleOrderbookMismatch();
        }
        break;
      case WebsocketMessage::MessageType::Unknown:
        break;
      }

      empty_spin_count = 0;
      continue;
    }

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

    // TESTING ORDERBOOK FOR NOW:
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count();
    std::optional<OrderbookStoreSnapshot> result{
        orderbook_store_.get(now_ms, Side::Yes)};

    std::cout << "Live feed:" << std::endl;
    if (!result) {
      std::cout << "No data found or invalid state" << std::endl;
    } else {
      std::cout << "Got data:" << std::endl;
      std::cout << "Timestamp: " << result->start_timestamp_ms_ << std::endl;
      for (std::size_t i{0}; i < 10; ++i) {
        std::cout << i << ": " << result->dollars_[i] << std::endl;
      }
    }

    std::optional<OrderbookStoreSnapshot> trailing_result{
        orderbook_store_.get(now_ms - 10'000, Side::Yes)};
    std::cout << "\n\nTrailing 10s:" << std::endl;
    if (!trailing_result) {
      std::cout << "No data found or invalid state" << std::endl;
    } else {
      std::cout << "Got data:" << std::endl;
      std::cout << "Timestamp: " << trailing_result->start_timestamp_ms_
                << std::endl;
      for (std::size_t i{0}; i < 10; ++i) {
        std::cout << i << ": " << trailing_result->dollars_[i] << std::endl;
      }
    }

    system("cls");
    // END TESTING

    cpuRelax();
  }
}

void Engine::handleOrderbookMismatch() {
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
  // Don't spam the request if one has recently been sent out
  if ((now_ms - last_orderbook_mismatch_request_) <=
      constants::REST_MESSAGE_COOLDOWN_MS)
    return;

  std::string host = "external-api.kalshi.com";

  std::string target = "/trade-api/v2/markets/";
  target += config_.market_ticker_;
  target += "/orderbook";

  rest_client_.get(host, target);

  last_orderbook_mismatch_request_ = now_ms;
}
