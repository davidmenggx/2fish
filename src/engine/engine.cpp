#include "engine.hpp"
#include "common/core/rest_data_types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/utils/compute_time_bucket.hpp"
#include "common/utils/cpu_relax.hpp"
#include "common/utils/set_query_params.hpp"
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
#include <utility>

Engine::Engine(moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
               Config config, std::atomic<bool> &running)
    : websocket_queue_{websocket_queue},
      candlestick_store_{rest_query_queue_, config},
      rest_client_{rest_patch_queue_}, config_{config}, running_{running} {}

void Engine::start() { thread_ = std::jthread(&Engine::run, this); }

void Engine::run() {
  WebsocketMessage websocket_message;
  RestMessage rest_patch_message;
  RestMessage rest_query_message;
  uint64_t empty_spin_count{0};
  while (running_.load(std::memory_order_relaxed)) {
    // Fixing any failed states takes priority over reading new data
    if (rest_patch_queue_.try_dequeue(rest_patch_message)) {
      switch (rest_patch_message.message_type_) {
      case RestMessage::MessageType::OrderbookSnapshot:
        orderbook_store_.tryPatch(rest_patch_message);
        break;
      case RestMessage::MessageType::Candlestick:
        candlestick_store_.tryPatch(rest_patch_message);
        break;
      case RestMessage::MessageType::Unknown:
        break;
      }
      empty_spin_count = 0;
      continue;
    }

    // Next prioritize live data
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
    }

    // Finally fill in historical data, if any
    if (rest_query_queue_.try_dequeue(rest_query_message)) {
      std::cout << "Filling historical\n";
      switch (rest_query_message.message_type_) {
      case RestMessage::MessageType::Candlestick:
        candlestick_store_.tryStoreHistorical(rest_query_message);
        break;
      // Not implemented VV
      case RestMessage::MessageType::OrderbookSnapshot:
      case RestMessage::MessageType::Unknown:
        break;
      }
      empty_spin_count = 0;
    }

    // TESTING START

    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count();
    std::optional<CandlestickStoreSnapshot> result{
        candlestick_store_.get(now_ms, Side::Yes)};

    std::cout << "Live feed:" << std::endl;
    if (!result) {
      std::cout << "No data found or invalid state" << std::endl;
    } else {
      std::cout << "Got data:" << std::endl;
      std::cout << "Timestamp: " << result->start_timestamp_ms_ << std::endl;
      std::cout << "Open: " << +result->open_ << ". High: " << +result->high_
                << ". Low: " << +result->low_ << ". Close: " << +result->close_
                << "\n\n";
    }

    std::optional<CandlestickStoreSnapshot> trailing_result{
        candlestick_store_.get(now_ms - 120'000, Side::Yes)};
    std::cout << "\n\nTrailing 2m:" << std::endl;
    if (!trailing_result) {
      std::cout << "No data found or invalid state" << std::endl;
    } else {
      std::cout << "Got data:" << std::endl;
      std::cout << "Timestamp: " << trailing_result->start_timestamp_ms_
                << std::endl;
      std::cout << "Open: " << +trailing_result->open_
                << ". High: " << +trailing_result->high_
                << ". Low: " << +trailing_result->low_
                << ". Close: " << +trailing_result->close_ << "\n\n";
    }

    system("cls");

    // TESTING END

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
      candlestick_store_.tryRolloverCandlestick(now_ms, Side::Yes);
      candlestick_store_.tryRolloverCandlestick(now_ms, Side::No);
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

  auto now_s =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();
  auto query_start_ts_s{
      computeTimeBucket(candlestick_store_.getLastValidTimestampMs(),
                        constants::CANDLESTICK_HISTORY_GRANULARITY_MS) /
      1000};

  setQueryParams(target, std::make_pair("start_ts", query_start_ts_s),
                 std::make_pair("end_ts", now_s),
                 std::make_pair("period_interval", 1));

  rest_client_.get(host, target);
}
