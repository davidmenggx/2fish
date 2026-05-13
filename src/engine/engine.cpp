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
#include <format>
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
        candlestick_store_.tryPatch(rest_message);
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
          handleCandlestickStoreMismatch();
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

  last_orderbook_mismatch_request_ = now_ms;

  std::string host = "external-api.kalshi.com";

  std::string target =
      std::format("/trade-api/v2/markets/{}/orderbook", config_.market_ticker_);

  rest_client_.get(host, target);
}

void Engine::handleCandlestickStoreMismatch() {
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
  // Don't spam the request if one has recently been sent out
  if ((now_ms - last_candlestick_mismatch_request_) <=
      constants::REST_MESSAGE_COOLDOWN_MS)
    return;
  last_candlestick_mismatch_request_ = now_ms;

  std::string host = "external-api.kalshi.com";

  std::string target =
      std::format("/trade-api/v2/series/{}/markets/{}/candlesticks",
                  config_.series_ticker_, config_.market_ticker_);

  rest_client_.get(host, target);
}
