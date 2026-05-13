#include "rest_parser.hpp"
#include "common/core/rest_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

RestParser::RestParser(
    moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue)
    : rest_patch_queue_{rest_patch_queue} {}

void RestParser::parseAndPush(simdjson::padded_string_view padded_json) {
  RestMessage message{};
  OrderbookSnapshotMessageRest orderbook_snapshot_accumulator{};
  CandlestickMessageRest candlestick_accumulator{};

  try {
    simdjson::ondemand::document doc{parser_.iterate(padded_json)};

    for (auto field : doc.get_object()) {
      std::string_view key;
      if (field.unescaped_key().get(key)) {
        std::cout << "Couldn't get JSON field, skipping\n";
        return;
      }

      simdjson::ondemand::value val;
      if (field.value().get(val)) {
        std::cout << "Couldn't get JSON value, skipping\n";
        return;
      }

      if (key == "orderbook_fp") {
        message.message_type_ = RestMessage::MessageType::OrderbookSnapshot;
        simdjson::ondemand::object message_body;
        if (val.get_object().get(message_body)) {
          continue;
        }

        for (auto body_field : message_body) {
          std::string_view body_key;
          if (body_field.unescaped_key().get(body_key)) {
            std::cout << "Couldn't get JSON field, skipping\n";
            return;
          }

          simdjson::ondemand::value inner_val;
          if (body_field.value().get(inner_val)) {
            std::cout << "Couldn't get JSON value, skipping\n";
            return;
          }

          if (body_key == "yes_dollars") {
            simdjson::ondemand::array arr;
            if (inner_val.get_array().get(arr)) {
              std::cout << "Couldn't get yes_dollars, skipping\n";
              return;
            }
            for (simdjson::ondemand::value element : arr) {
              simdjson::ondemand::array pair;
              if (element.get_array().get(pair)) {
                std::cout << "Couldn't get inner level array, skipping\n";
                return;
              }

              std::string_view price_str, vol_str;
              std::size_t idx{0};

              for (simdjson::ondemand::value pair_val : pair) {
                if (idx == 0) {
                  if (pair_val.get_string().get(price_str)) {
                    std::cout << "Couldn't get price string, skipping\n";
                    return;
                  }
                } else if (idx == 1) {
                  if (pair_val.get_string().get(vol_str)) {
                    std::cout << "Couldn't get volume string, skipping\n";
                    return;
                  }
                }
                idx++;
              }

              if (idx < 2) {
                std::cout << "Incomplete pair array, skipping\n";
                return;
              }

              double tmp_price_level{std::stod(std::string(price_str))};
              double tmp_volume{std::stod(std::string(vol_str))};

              uint8_t price_level =
                  static_cast<uint8_t>(std::round(tmp_price_level * 100));
              orderbook_snapshot_accumulator.yes_dollars_[price_level] +=
                  static_cast<long double>(tmp_volume);
            }
          }
          if (body_key == "no_dollars") {
            simdjson::ondemand::array arr;
            if (inner_val.get_array().get(arr)) {
              std::cout << "Couldn't get no_dollars, skipping\n";
              return;
            }
            for (simdjson::ondemand::value element : arr) {
              simdjson::ondemand::array pair;
              if (element.get_array().get(pair)) {
                std::cout << "Couldn't get inner level array, skipping\n";
                return;
              }

              std::string_view price_str, vol_str;
              std::size_t idx{0};

              for (simdjson::ondemand::value pair_val : pair) {
                if (idx == 0) {
                  if (pair_val.get_string().get(price_str)) {
                    std::cout << "Couldn't get price string, skipping\n";
                    return;
                  }
                } else if (idx == 1) {
                  if (pair_val.get_string().get(vol_str)) {
                    std::cout << "Couldn't get volume string, skipping\n";
                    return;
                  }
                }
                idx++;
              }

              if (idx < 2) {
                std::cout << "Incomplete pair array, skipping\n";
                return;
              }

              double tmp_price_level{std::stod(std::string(price_str))};
              double tmp_volume{std::stod(std::string(vol_str))};

              uint8_t price_level =
                  static_cast<uint8_t>(std::round(tmp_price_level * 100));
              orderbook_snapshot_accumulator.no_dollars_[price_level] +=
                  static_cast<long double>(tmp_volume);
            }
          }
        }
      }
      if (key == "candlesticks") {
        // TODO
      }
    }
  } catch (const simdjson::simdjson_error &e) {
    std::cerr << "JSON parsing error: " << e.what() << '\n';
  }

  switch (message.message_type_) {
  case RestMessage::MessageType::Unknown:
    std::cout << "Couldn't find the message type, skipping\n";
    return;
  case RestMessage::MessageType::OrderbookSnapshot:
    // TODO: Perhaps it is not ideal to ask for time here
    // Min is a sentinel for no timestamp (predates the Big Bang as of 2026)
    if (orderbook_snapshot_accumulator.timestamp_ms_ ==
        std::numeric_limits<int64_t>::min()) {
      auto now = std::chrono::system_clock::now();
      orderbook_snapshot_accumulator.timestamp_ms_ =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              now.time_since_epoch())
              .count();
    }
    message.body_ = orderbook_snapshot_accumulator;
    break;
  case RestMessage::MessageType::Candlestick:
    message.body_ = candlestick_accumulator;
    break;
  }

  rest_patch_queue_.try_emplace(message);
}
