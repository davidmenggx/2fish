#include "common/core/types.hpp"
#include "common/core/websocket_data_types.hpp"
#include "network/websocket/websocket_parser.hpp"

#include <gtest/gtest.h>

#include "moodycamel/readerwriterqueue.h"
#include <simdjson.h>

#include <string>
#include <variant>

// Test success cases

TEST(WebsocketParserTest, ParsesValidOrderbookSnapshot) {
  moodycamel::ReaderWriterQueue<WebsocketMessage> queue{};
  WebsocketParser parser{queue};

  const std::string raw_json =
      R"({"type":"orderbook_snapshot","sid":1,"seq":55,"msg":{"market_ticker":"TEST","market_id":"test-id","yes_dollars_fp":[["0.0100","91.74"],["0.0110","91.74"],["0.0500","1.00"],["0.0600","8.37"],["0.4000","2695.00"]],"no_dollars_fp":[["0.0100","50196.68"],["0.1200","8.00"],["1.00","2.34"]]}})";
  simdjson::padded_string valid_orderbook_snapshot(raw_json);

  parser.parseAndPush(valid_orderbook_snapshot);

  WebsocketMessage result{};
  ASSERT_TRUE(queue.try_dequeue(result)) << "Failed to dequeue parsed message";
  WebsocketMessage extraneous_result{};
  ASSERT_FALSE(queue.try_dequeue(extraneous_result))
      << "Extraneous message in queue";

  EXPECT_EQ(result.message_type_,
            WebsocketMessage::MessageType::OrderbookSnapshot);
  EXPECT_EQ(result.sequence_id_, 55);

  const auto *result_body =
      std::get_if<OrderbookSnapshotMessageWs>(&result.body_);
  ASSERT_NE(result_body, nullptr)
      << "Message body is not an OrderbookSnapshotMessageWs";

  OrderbookSnapshotMessageWs expected{};
  expected.market_ticker_ = "TEST";
  expected.market_id_ = "test-id";

  expected.yes_dollars_[1] = 183.48;
  expected.yes_dollars_[5] = 1.00;
  expected.yes_dollars_[6] = 8.37;
  expected.yes_dollars_[40] = 2'695.00;

  expected.no_dollars_[1] = 50'196.68;
  expected.no_dollars_[12] = 8.00;
  expected.no_dollars_[100] = 2.34;

  EXPECT_EQ(*result_body, expected);
}

TEST(WebsocketParserTest, ParsesValidOrderbookDelta) {
  moodycamel::ReaderWriterQueue<WebsocketMessage> queue{};
  WebsocketParser parser{queue};

  const std::string raw_json =
      R"({"type":"orderbook_delta","sid":1,"seq":66,"msg":{"market_ticker":"TEST","market_id":"test-id","price_dollars":"0.0400","delta_fp":"-0.05","side":"no","ts":"2026-05-06T00:51:22.875661Z","ts_ms":1778028682875}})";
  simdjson::padded_string valid_orderbook_delta(raw_json);

  parser.parseAndPush(valid_orderbook_delta);

  WebsocketMessage result{};
  ASSERT_TRUE(queue.try_dequeue(result)) << "Failed to dequeue parsed message";
  WebsocketMessage extraneous_result{};
  ASSERT_FALSE(queue.try_dequeue(extraneous_result))
      << "Extraneous message in queue";

  EXPECT_EQ(result.message_type_,
            WebsocketMessage::MessageType::OrderbookDelta);
  EXPECT_EQ(result.sequence_id_, 66);

  const auto *result_body = std::get_if<OrderbookDeltaMessageWs>(&result.body_);
  ASSERT_NE(result_body, nullptr)
      << "Message body is not an OrderbookDeltaMessageWs";

  OrderbookDeltaMessageWs expected{};
  expected.market_ticker_ = "TEST";
  expected.market_id_ = "test-id";
  expected.price_cents_ = 4;
  expected.delta_ = -0.05;
  expected.side_ = Side::No;
  expected.timestamp_ms_ = 1778028682875;

  EXPECT_EQ(*result_body, expected);
}

TEST(WebsocketParserTest, ParsesValidTrade) {
  moodycamel::ReaderWriterQueue<WebsocketMessage> queue{};
  WebsocketParser parser{queue};

  const std::string raw_json =
      R"({"type":"trade","sid":1,"seq":77,"msg":{"trade_id":"test-id","market_ticker":"TEST","yes_price_dollars":"0.8600","no_price_dollars":"0.1400","count_fp":"100.00","taker_side":"yes","ts":1778028729,"ts_ms":1778028729142}})";
  simdjson::padded_string valid_trade(raw_json);

  parser.parseAndPush(valid_trade);

  WebsocketMessage result{};
  ASSERT_TRUE(queue.try_dequeue(result)) << "Failed to dequeue parsed message";
  WebsocketMessage extraneous_result{};
  ASSERT_FALSE(queue.try_dequeue(extraneous_result))
      << "Extraneous message in queue";

  EXPECT_EQ(result.message_type_,
            WebsocketMessage::MessageType::Trade);
  EXPECT_EQ(result.sequence_id_, 77);

  const auto *result_body = std::get_if<TradeMessageWs>(&result.body_);
  ASSERT_NE(result_body, nullptr)
      << "Message body is not an TradeMessageWs";

  TradeMessageWs expected{};
  expected.trade_id_ = "test-id";
  expected.market_ticker_ = "TEST";
  expected.yes_price_cents_ = 86;
  expected.no_price_cents_ = 14;
  expected.contracts_traded_ = 100.00;
  expected.taker_side_ = Side::Yes;
  expected.timestamp_ms_ = 1778028729142;

  EXPECT_EQ(*result_body, expected);
}

// Test failure cases

TEST(WebsocketParserTest, DropsMalformedMessage) {
  moodycamel::ReaderWriterQueue<WebsocketMessage> queue{};
  WebsocketParser parser{queue};

  const std::string raw_json =
      R"({"type":"orderbook_delta""sid":1,"seq":66,"msg":{"market_ticker":"TEST","market_id":"test-id","price_dollars":"0.0400","delta_fp":"-0.05","side":"no","ts":"2026-05-06T00:51:22.875661Z","ts_ms":1778028682875})";
  simdjson::padded_string malformed_message(raw_json);

  parser.parseAndPush(malformed_message);

  WebsocketMessage result{};
  EXPECT_EQ(queue.try_dequeue(result), false);
}

TEST(WebsocketParserTest, DropsUnknownMessageType) {
  moodycamel::ReaderWriterQueue<WebsocketMessage> queue{};
  WebsocketParser parser{queue};

  const std::string raw_json =
      R"({"type":"unknown_type","sid":1,"seq":66,"msg":{"market_ticker":"TEST","market_id":"test-id","price_dollars":"0.0400","delta_fp":"-0.05","side":"no","ts":"2026-05-06T00:51:22.875661Z","ts_ms":1778028682875}})";
  simdjson::padded_string invalid_type_message(raw_json);

  parser.parseAndPush(invalid_type_message);

  WebsocketMessage result{};
  EXPECT_EQ(queue.try_dequeue(result), false);
}
