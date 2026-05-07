#include "websocket_parser.hpp"
#include "common/websocket_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <charconv>

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
            }
          }
          if (body_key == "market_id") {
            std::string_view market_id;
            if (!inner_val.get_string().get(market_id)) {
              orderbook_snapshot_accumulator.market_id_ = market_id;
              orderbook_delta_accumulator.market_id_ = market_id;
            }
          }
          if (body_key == "ts_ms") {
            int64_t timestamp_ms{};
            if (!inner_val.get_int64().get(timestamp_ms)) {
              orderbook_delta_accumulator.timestamp_ms_ = timestamp_ms;
              trade_accumulator.timestamp_ms_ = timestamp_ms;
            }
          }
          // Fields specific to orderbook_delta type
          if (body_key == "price_dollars") {
            double price_dollars{};
            if (!inner_val.get_double().get(price_dollars)) {
              orderbook_delta_accumulator.price_cents_ =
                  std::round(price_dollars * 100);
            }
          }
          if (body_key == "delta_fp") {
            double delta_fp{};
            if (!inner_val.get_double().get(delta_fp)) {
              orderbook_delta_accumulator.delta_ = delta_fp;
            }
          }
          if (body_key == "side") {
            std::string_view side;
            if (!inner_val.get_string().get(side)) {
              if (side == "yes")
                orderbook_delta_accumulator.side_ = Side::Yes;
              if (side == "no")
                orderbook_delta_accumulator.side_ = Side::No;
            }
          }
          // Fields specific to trade type
          if (body_key == "trade_id") {
            std::string_view trade_id;
            if (!inner_val.get_string().get(trade_id)) {
              trade_accumulator.trade_id_ = trade_id;
            }
          }
          if (body_key == "yes_price_dollars") {
            double yes_price_dollars{};
            if (!inner_val.get_double().get(yes_price_dollars)) {
              trade_accumulator.yes_price_cents_ =
                  std::round(yes_price_dollars * 100);
            }
          }
          if (body_key == "no_price_dollars") {
            double no_price_dollars{};
            if (!inner_val.get_double().get(no_price_dollars)) {
              trade_accumulator.no_price_cents_ =
                  std::round(no_price_dollars * 100);
            }
          }
          if (body_key == "count_fp") {
            double count_fp{};
            if (!inner_val.get_double().get(count_fp)) {
              trade_accumulator.contracts_traded_ = count_fp;
            }
          }
          if (body_key == "taker_side") {
            std::string_view taker_side;
            if (!inner_val.get_string().get(taker_side)) {
              if (taker_side == "yes")
                trade_accumulator.taker_side_ = Side::Yes;
              if (taker_side == "no")
                trade_accumulator.taker_side_ = Side::No;
            }
          }
          // Fields specific to orderbook_snapshot type
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
