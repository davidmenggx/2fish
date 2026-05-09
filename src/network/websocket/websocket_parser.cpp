#include "websocket_parser.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

WebsocketParser::WebsocketParser(
    moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue)
    : websocket_queue_{websocket_queue} {}

void WebsocketParser::parseAndPush(simdjson::padded_string_view padded_json) {
  WebsocketMessage message{};
  OrderbookSnapshotMessage orderbook_snapshot_accumulator{};
  OrderbookDeltaMessage orderbook_delta_accumulator{};
  TradeMessage trade_accumulator{};

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

      if (key == "type") {
        std::string_view message_type_str;
        if (!val.get_string().get(message_type_str)) {
          message.message_type_ = getWebsocketMessageType(message_type_str);
        }
      }
      if (key == "seq") {
        uint64_t sequence_id;
        if (!val.get_uint64().get(sequence_id)) {
          message.sequence_id_ = sequence_id;
        }
      }
      if (key == "msg") {
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

          if (body_key == "market_ticker") {
            std::string_view market_ticker;
            if (!inner_val.get_string().get(market_ticker)) {
              orderbook_snapshot_accumulator.market_ticker_ = market_ticker;
              orderbook_delta_accumulator.market_ticker_ = market_ticker;
              trade_accumulator.market_ticker_ = market_ticker;
            } else {
              std::cout << "Couldn't get market_ticker, skipping\n";
              return;
            }
          }
          if (body_key == "market_id") {
            std::string_view market_id;
            if (!inner_val.get_string().get(market_id)) {
              orderbook_snapshot_accumulator.market_id_ = market_id;
              orderbook_delta_accumulator.market_id_ = market_id;
            } else {
              std::cout << "Couldn't get market_id, skipping\n";
              return;
            }
          }
          if (body_key == "ts_ms") {
            int64_t timestamp_ms{};
            if (!inner_val.get_int64().get(timestamp_ms)) {
              orderbook_delta_accumulator.timestamp_ms_ = timestamp_ms;
              trade_accumulator.timestamp_ms_ = timestamp_ms;
            } else {
              std::cout << "Couldn't get ts_ms, skipping\n";
              return;
            }
          }
          // Fields specific to orderbook_delta type
          if (body_key == "price_dollars") {
            double price_dollars{};
            if (!inner_val.get_double().get(price_dollars)) {
              orderbook_delta_accumulator.price_cents_ =
                  std::round(price_dollars * 100);
            } else {
              std::cout << "Couldn't get price_dollars, skipping\n";
              return;
            }
          }
          if (body_key == "delta_fp") {
            double delta_fp{};
            if (!inner_val.get_double().get(delta_fp)) {
              orderbook_delta_accumulator.delta_ = delta_fp;
            } else {
              std::cout << "Couldn't get delta_fp, skipping\n";
              return;
            }
          }
          if (body_key == "side") {
            std::string_view side;
            if (!inner_val.get_string().get(side)) {
              if (side == "yes")
                orderbook_delta_accumulator.side_ = Side::Yes;
              if (side == "no")
                orderbook_delta_accumulator.side_ = Side::No;
            } else {
              std::cout << "Couldn't get side, skipping\n";
              return;
            }
          }
          // Fields specific to trade type
          if (body_key == "trade_id") {
            std::string_view trade_id;
            if (!inner_val.get_string().get(trade_id)) {
              trade_accumulator.trade_id_ = trade_id;
            } else {
              std::cout << "Couldn't get trade_id, skipping\n";
              return;
            }
          }
          if (body_key == "yes_price_dollars") {
            double yes_price_dollars{};
            if (!inner_val.get_double().get(yes_price_dollars)) {
              trade_accumulator.yes_price_cents_ =
                  std::round(yes_price_dollars * 100);
            } else {
              std::cout << "Couldn't get yes_price_dollars, skipping\n";
              return;
            }
          }
          if (body_key == "no_price_dollars") {
            double no_price_dollars{};
            if (!inner_val.get_double().get(no_price_dollars)) {
              trade_accumulator.no_price_cents_ =
                  std::round(no_price_dollars * 100);
            } else {
              std::cout << "Couldn't get no_price_dollars, skipping\n";
              return;
            }
          }
          if (body_key == "count_fp") {
            double count_fp{};
            if (!inner_val.get_double().get(count_fp)) {
              trade_accumulator.contracts_traded_ = count_fp;
            } else {
              std::cout << "Couldn't get count_fp, skipping\n";
              return;
            }
          }
          if (body_key == "taker_side") {
            std::string_view taker_side;
            if (!inner_val.get_string().get(taker_side)) {
              if (taker_side == "yes")
                trade_accumulator.taker_side_ = Side::Yes;
              if (taker_side == "no")
                trade_accumulator.taker_side_ = Side::No;
            } else {
              std::cout << "Couldn't get taker_side, skipping\n";
              return;
            }
          }
          // Fields specific to orderbook_snapshot type
          if (body_key == "yes_dollars_fp") {
            simdjson::ondemand::array arr;
            if (val.get_array().get(arr)) {
              std::cout << "Couldn't get yes_dollars_fp, skipping\n";
              return;
            }
            for (simdjson::ondemand::value element : arr) {
              simdjson::ondemand::array pair;
              if (element.get_array().get(pair)) {
                std::cout
                    << "Couldn't get yes_dollars_fp inner level, skipping\n";
                return;
              }
              double tmp_price_level{};
              double tmp_volume{};

              auto price_level_err{
                  pair.at(0).get_double().get(tmp_price_level)};

              auto volume_err{pair.at(1).get_double().get(tmp_volume)};

              if (price_level_err || volume_err) {
                std::cout
                    << "Couldn't get yes_dollars_fp inner level, skipping\n";
                return;
              }
              uint8_t price_level{
                  static_cast<uint8_t>(std::round(tmp_price_level * 100))};
              orderbook_snapshot_accumulator.yes_dollars_[price_level] +=
                  static_cast<long double>(tmp_volume);
            }
          }
          if (body_key == "no_dollars_fp") {
            simdjson::ondemand::array arr;
            if (val.get_array().get(arr)) {
              std::cout << "Couldn't get no_dollars_fp, skipping\n";
              return;
            }
            for (simdjson::ondemand::value element : arr) {
              simdjson::ondemand::array pair;
              if (element.get_array().get(pair)) {
                std::cout
                    << "Couldn't get no_dollars_fp inner level, skipping\n";
                return;
              }
              double tmp_price_level{};
              double tmp_volume{};

              auto price_level_err{
                  pair.at(0).get_double().get(tmp_price_level)};

              auto volume_err{pair.at(1).get_double().get(tmp_volume)};

              if (price_level_err || volume_err) {
                std::cout
                    << "Couldn't get no_dollars_fp inner level, skipping\n";
                return;
              }
              uint8_t price_level{
                  static_cast<uint8_t>(std::round(tmp_price_level * 100))};
              orderbook_snapshot_accumulator.no_dollars_[price_level] +=
                  static_cast<long double>(tmp_volume);
            }
          }
        }
      }
    }
  } catch (const simdjson::simdjson_error &e) {
    std::cerr << "JSON parsing error: " << e.what() << '\n';
  }

  switch (message.message_type_) {
  case WebsocketMessage::MessageType::Unknown:
    std::cout << "Couldn't find the message type, skipping\n";
    return;
  case WebsocketMessage::MessageType::OrderbookSnapshot:
    message.body_ = orderbook_snapshot_accumulator;
    break;
  case WebsocketMessage::MessageType::OrderbookDelta:
    message.body_ = orderbook_delta_accumulator;
    break;
  case WebsocketMessage::MessageType::Trade:
    message.body_ = trade_accumulator;
    break;
  }

  websocket_queue_.try_enqueue(message);
}
