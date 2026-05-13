#include "websocket_parser.hpp"
#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "common/utils/parse_json_double.hpp"

#include "moodycamel/readerwriterqueue.h"
#include <simdjson.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

WebsocketParser::WebsocketParser(
    moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue)
    : websocket_queue_{websocket_queue} {}

void WebsocketParser::parseAndPush(simdjson::padded_string_view padded_json) {
  try {
    simdjson::ondemand::document doc = parser_.iterate(padded_json);
    simdjson::ondemand::object root = doc.get_object();

    WebsocketMessage message{};

    std::string_view type_str = root["type"].get_string();
    message.message_type_ = getWebsocketMessageType(type_str);

    if (message.message_type_ == WebsocketMessage::MessageType::Unknown) {
      std::cerr << "Unknown message type, skipping\n";
      return;
    }

    message.sequence_id_ = root["seq"].get_uint64();

    simdjson::ondemand::object msg = root["msg"].get_object();

    switch (message.message_type_) {
    case WebsocketMessage::MessageType::OrderbookSnapshot:
      message.body_ = parseOrderbookSnapshot(msg);
      break;
    case WebsocketMessage::MessageType::OrderbookDelta:
      message.body_ = parseOrderbookDelta(msg);
      break;
    case WebsocketMessage::MessageType::Trade:
      message.body_ = parseTrade(msg);
      break;
    default:
      return;
    }

    websocket_queue_.try_enqueue(message);

  } catch (const simdjson::simdjson_error &e) {
    std::cerr << "JSON parsing error: " << e.what() << '\n';
  }
}

OrderbookSnapshotMessageWs
WebsocketParser::parseOrderbookSnapshot(simdjson::ondemand::object &msg) {
  OrderbookSnapshotMessageWs snapshot{};

  for (auto field : msg) {
    std::string_view key = field.unescaped_key();

    if (key == "market_ticker") {
      std::string_view ticker_sv = field.value().get_string();
      snapshot.market_ticker_ = ticker_sv;
    } else if (key == "market_id") {
      std::string_view id_sv = field.value().get_string();
      snapshot.market_id_ = id_sv;
    } else if (key == "yes_dollars_fp" || key == "no_dollars_fp") {
      bool is_yes = (key == "yes_dollars_fp");
      auto &dollars_array =
          is_yes ? snapshot.yes_dollars_ : snapshot.no_dollars_;

      for (auto element : field.value().get_array()) {
        auto pair = element.get_array();
        auto it = pair.begin();

        std::string_view price_str = (*it).get_string();
        ++it;
        std::string_view vol_str = (*it).get_string();

        double price = parseJsonDouble(price_str);
        double volume = parseJsonDouble(vol_str);

        uint8_t price_level = static_cast<uint8_t>(std::round(price * 100.0));
        dollars_array[price_level] += static_cast<long double>(volume);
      }
    }
  }
  return snapshot;
}

OrderbookDeltaMessageWs
WebsocketParser::parseOrderbookDelta(simdjson::ondemand::object &msg) {
  OrderbookDeltaMessageWs delta{};

  for (auto field : msg) {
    std::string_view key = field.unescaped_key();

    if (key == "market_ticker") {
      std::string_view ticker_sv = field.value().get_string();
      delta.market_ticker_ = ticker_sv;
    } else if (key == "market_id") {
      std::string_view id_sv = field.value().get_string();
      delta.market_id_ = id_sv;
    } else if (key == "ts_ms") {
      delta.timestamp_ms_ = field.value().get_int64();
    } else if (key == "price_dollars") {
      double price = parseJsonDouble(field.value().get_string());
      delta.price_cents_ = std::round(price * 100.0);
    } else if (key == "delta_fp") {
      delta.delta_ = parseJsonDouble(field.value().get_string());
    } else if (key == "side") {
      delta.side_ = parseSide(field.value().get_string());
    }
  }
  return delta;
}

TradeMessageWs WebsocketParser::parseTrade(simdjson::ondemand::object &msg) {
  TradeMessageWs trade{};

  for (auto field : msg) {
    std::string_view key = field.unescaped_key();

    if (key == "market_ticker") {
      std::string_view ticker_sv = field.value().get_string();
      trade.market_ticker_ = ticker_sv;
    } else if (key == "ts_ms") {
      trade.timestamp_ms_ = field.value().get_int64();
    } else if (key == "trade_id") {
      std::string_view trade_id_sv = field.value().get_string();
      trade.trade_id_ = trade_id_sv;
    } else if (key == "yes_price_dollars") {
      double price = parseJsonDouble(field.value().get_string());
      trade.yes_price_cents_ = std::round(price * 100.0);
    } else if (key == "no_price_dollars") {
      double price = parseJsonDouble(field.value().get_string());
      trade.no_price_cents_ = std::round(price * 100.0);
    } else if (key == "count_fp") {
      trade.contracts_traded_ = parseJsonDouble(field.value().get_string());
    } else if (key == "taker_side") {
      trade.taker_side_ = parseSide(field.value().get_string());
    }
  }
  return trade;
}
