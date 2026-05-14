#include "rest_parser.hpp"
#include "common/core/rest_data_types.hpp"
#include "common/utils/parse_json_double.hpp"
#include "common/utils/price_round.hpp"

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
    moodycamel::ReaderWriterQueue<RestMessage> &output_data_queue)
    : output_data_queue_{output_data_queue} {}

void RestParser::parseAndPush(simdjson::padded_string_view padded_json) {
  RestMessage message{};
  message.message_type_ = RestMessage::MessageType::Unknown;

  OrderbookSnapshotMessageRest orderbook_snapshot_accumulator{};
  CandlestickMessageRest candlestick_accumulator{};

  try {
    thread_local simdjson::ondemand::parser parser_;
    simdjson::ondemand::document doc = parser_.iterate(padded_json);

    for (auto field : doc.get_object()) {
      std::string_view key = field.unescaped_key();

      if (key == "orderbook_fp") {
        message.message_type_ = RestMessage::MessageType::OrderbookSnapshot;
        simdjson::ondemand::object message_body = field.value().get_object();

        for (auto body_field : message_body) {
          std::string_view body_key = body_field.unescaped_key();

          if (body_key == "yes_dollars" || body_key == "no_dollars") {
            bool is_yes = (body_key == "yes_dollars");
            auto &dollars_array =
                is_yes ? orderbook_snapshot_accumulator.yes_dollars_
                       : orderbook_snapshot_accumulator.no_dollars_;

            for (auto element : body_field.value().get_array()) {
              auto pair = element.get_array();
              auto it = pair.begin();

              std::string_view price_str = (*it).get_string();
              ++it;
              std::string_view vol_str = (*it).get_string();

              double tmp_price_level = parseJsonDouble(price_str);
              double tmp_volume = parseJsonDouble(vol_str);

              uint8_t price_level =
                  static_cast<uint8_t>(priceRound(tmp_price_level * 100.0));
              dollars_array[price_level] +=
                  static_cast<long double>(tmp_volume);
            }
          }
        }
      } else if (key == "candlesticks") {
        message.message_type_ = RestMessage::MessageType::Candlestick;

        for (auto inner_candlestick : field.value().get_array()) {
          CandlestickMessageRest::Candlestick current_candlestick{};

          for (auto inner_candlestick_field : inner_candlestick.get_object()) {
            std::string_view inner_candlestick_key =
                inner_candlestick_field.unescaped_key();

            if (inner_candlestick_key == "end_period_ts") {
              current_candlestick.end_period_ts_s_ =
                  inner_candlestick_field.value().get_int64();
            } else if (inner_candlestick_key == "price") {
              for (auto inner_price_field :
                   inner_candlestick_field.value().get_object()) {
                std::string_view inner_price_key =
                    inner_price_field.unescaped_key();

                if (inner_price_key == "open_dollars") {
                  double open_dollars =
                      parseJsonDouble(inner_price_field.value().get_string());
                  current_candlestick.open_cents_ =
                      priceRound(open_dollars * 100.0);
                } else if (inner_price_key == "high_dollars") {
                  double high_dollars =
                      parseJsonDouble(inner_price_field.value().get_string());
                  current_candlestick.high_cents_ =
                      priceRound(high_dollars * 100.0);
                } else if (inner_price_key == "low_dollars") {
                  double low_dollars =
                      parseJsonDouble(inner_price_field.value().get_string());
                  current_candlestick.low_cents_ =
                      priceRound(low_dollars * 100.0);
                } else if (inner_price_key == "close_dollars") {
                  double close_dollars =
                      parseJsonDouble(inner_price_field.value().get_string());
                  current_candlestick.close_cents_ =
                      priceRound(close_dollars * 100.0);
                }
              }
            }
          }
          candlestick_accumulator.candlesticks_.push_back(current_candlestick);
        }
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

  output_data_queue_.try_emplace(message);
}
