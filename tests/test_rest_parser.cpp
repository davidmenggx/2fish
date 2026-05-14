#include "common/core/rest_data_types.hpp"
#include "common/core/types.hpp"
#include "network/rest/rest_parser.hpp"

#include <gtest/gtest.h>

#include "moodycamel/readerwriterqueue.h"
#include <simdjson.h>

#include <string>
#include <variant>

// Test success cases

TEST(RestParserTest, ParsesValidOrderbookSnapshot) {
  moodycamel::ReaderWriterQueue<RestMessage> queue{};
  RestParser parser{queue};

  const std::string raw_json =
      R"({"orderbook_fp": {"no_dollars": [["0.0010", "1000.00"], ["0.0090", "3200.00"], ["0.4500", "2000.00"], ["0.5000", "2857.00"], ["1.0000", "4.00"]], "yes_dollars": [["0.0010", "1000.00"], ["0.0030", "82000.00"], ["0.0510", "2500.00"]]}})";
  simdjson::padded_string valid_orderbook_snapshot(raw_json);

  parser.parseAndPush(valid_orderbook_snapshot);

  RestMessage result{};
  ASSERT_TRUE(queue.try_dequeue(result)) << "Failed to dequeue parsed message";
  RestMessage extraneous_result{};
  ASSERT_FALSE(queue.try_dequeue(extraneous_result))
      << "Extraneous message in queue";

  EXPECT_EQ(result.message_type_, RestMessage::MessageType::OrderbookSnapshot);
  const auto *result_body =
      std::get_if<OrderbookSnapshotMessageRest>(&result.body_);
  ASSERT_NE(result_body, nullptr)
      << "Message body is not an OrderbookSnapshotMessageRest";

  OrderbookSnapshotMessageRest expected{};
  expected.yes_dollars_[0] = 83'000.00;
  expected.yes_dollars_[5] = 2'500;

  expected.no_dollars_[0] = 4'200.00;
  expected.no_dollars_[45] = 2'000.00;
  expected.no_dollars_[50] = 2'857.00;
  expected.no_dollars_[100] = 4.00;

  expected.timestamp_ms_ = result_body->timestamp_ms_;

  EXPECT_EQ(*result_body, expected);
}

TEST(RestParserTest, ParsesValidCandlestick) {
  moodycamel::ReaderWriterQueue<RestMessage> queue{};
  RestParser parser{queue};

  const std::string raw_json =
      R"({"candlesticks":[{"end_period_ts":1778710380,"open_interest_fp":"1656034.48","price":{"close_dollars":"0.7000","high_dollars":"0.7900","low_dollars":"0.6700","mean_dollars":"0.7244","open_dollars":"0.7700","previous_dollars":"0.7700"},"volume_fp":"12665.04","yes_ask":{"close_dollars":"0.7400","high_dollars":"0.7900","low_dollars":"0.6700","open_dollars":"0.7700"},"yes_bid":{"close_dollars":"0.7000","high_dollars":"0.7600","low_dollars":"0.6600","open_dollars":"0.7600"}},{"end_period_ts":1778710440,"open_interest_fp":"1673280.98","price":{"close_dollars":"0.5800","high_dollars":"0.7600","low_dollars":"0.5800","mean_dollars":"0.6492","open_dollars":"0.7400","previous_dollars":"0.7000"},"volume_fp":"30203.09","yes_ask":{"close_dollars":"0.5800","high_dollars":"0.7600","low_dollars":"0.5800","open_dollars":"0.7400"},"yes_bid":{"close_dollars":"0.5700","high_dollars":"0.7100","low_dollars":"0.5700","open_dollars":"0.7000"}}],"ticker":"KXATPMATCH-26MAY13JODDAR-JOD"})";
  simdjson::padded_string valid_trade(raw_json);

  parser.parseAndPush(valid_trade);

  RestMessage result{};
  ASSERT_TRUE(queue.try_dequeue(result)) << "Failed to dequeue parsed message";
  RestMessage extraneous_result{};
  ASSERT_FALSE(queue.try_dequeue(extraneous_result))
      << "Extraneous message in queue";

  EXPECT_EQ(result.message_type_, RestMessage::MessageType::Candlestick);

  const auto *result_body = std::get_if<CandlestickMessageRest>(&result.body_);
  ASSERT_NE(result_body, nullptr) << "Message body is not an CandlestickMessageRest";

  CandlestickMessageRest::Candlestick expected1{};
  expected1.end_period_ts_s_ = 1778710380LL;
  expected1.open_cents_ = 77;
  expected1.high_cents_ = 79;
  expected1.low_cents_ = 67;
  expected1.close_cents_ = 70;

  CandlestickMessageRest::Candlestick expected2{};
  expected2.end_period_ts_s_ = 1778710440LL;
  expected2.open_cents_ = 74;
  expected2.high_cents_ = 76;
  expected2.low_cents_ = 58;
  expected2.close_cents_ = 58;

  CandlestickMessageRest expected{};
  expected.candlesticks_.push_back(expected1);
  expected.candlesticks_.push_back(expected2);

  EXPECT_EQ(*result_body, expected);
}

// Test failure cases

TEST(RestParserTest, DropsMalformedMessage) {
  moodycamel::ReaderWriterQueue<RestMessage> queue{};
  RestParser parser{queue};

  const std::string raw_json =
      R"({"orderbook_fp" "no_dollars": [["0.0010", "1000.00"], ["0.0090", "3200.00"], ["0.4500", "2000.00"], ["0.5000", "2857.00"], ["1.0000", "4.00"]] "yes_dollars": [["0.0010", "1000.00"], ["0.0030", "82000.00"], ["0.0510", "2500.00"]]}})";
  simdjson::padded_string malformed_message(raw_json);

  parser.parseAndPush(malformed_message);

  RestMessage result{};
  EXPECT_EQ(queue.try_dequeue(result), false);
}

TEST(RestParserTest, DropsUnknownMessageType) {
  moodycamel::ReaderWriterQueue<RestMessage> queue{};
  RestParser parser{queue};

  const std::string raw_json =
      R"({"unknown_type":[{"end_period_ts":1778710380,"open_interest_fp":"1656034.48","price":{"close_dollars":"0.7000","high_dollars":"0.7900","low_dollars":"0.6700","mean_dollars":"0.7244","open_dollars":"0.7700","previous_dollars":"0.7700"},"volume_fp":"12665.04","yes_ask":{"close_dollars":"0.7400","high_dollars":"0.7900","low_dollars":"0.6700","open_dollars":"0.7700"},"yes_bid":{"close_dollars":"0.7000","high_dollars":"0.7600","low_dollars":"0.6600","open_dollars":"0.7600"}},{"end_period_ts":1778710440,"open_interest_fp":"1673280.98","price":{"close_dollars":"0.5800","high_dollars":"0.7600","low_dollars":"0.5800","mean_dollars":"0.6492","open_dollars":"0.7400","previous_dollars":"0.7000"},"volume_fp":"30203.09","yes_ask":{"close_dollars":"0.5800","high_dollars":"0.7600","low_dollars":"0.5800","open_dollars":"0.7400"},"yes_bid":{"close_dollars":"0.5700","high_dollars":"0.7100","low_dollars":"0.5700","open_dollars":"0.7000"}}],"ticker":"KXATPMATCH-26MAY13JODDAR-JOD"})";
  simdjson::padded_string invalid_type_message(raw_json);

  parser.parseAndPush(invalid_type_message);

  RestMessage result{};
  EXPECT_EQ(queue.try_dequeue(result), false);
}
