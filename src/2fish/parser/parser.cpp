#include "2fish/models/trade.h"
#include "2fish/models/types.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/parser/parser.h"
#include "2fish/parser/parser_buffer_pool.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <format>
#include <intrin0.inl.h>
#include <iostream>
#include <string>

parser::Parser::Parser(moodycamel::ReaderWriterQueue<market::MessageBuffer*>&network_queue,
	moodycamel::ReaderWriterQueue<market::MarketAccumulation*>&engine_queue,
	moodycamel::ReaderWriterQueue<market::Trade>&trade_queue,
	market::NetworkBufferPool & network_buffer_pool, parser::ParserBufferPool & parser_buffer_pool,
	std::atomic<bool>&running, std::string target_asset_id)
	: network_queue_{ network_queue }, network_buffer_pool_{ network_buffer_pool }
	, engine_queue_{ engine_queue }, trade_queue_{ trade_queue }
	, parser_buffer_pool_{ parser_buffer_pool }, running_{ running }
	, target_asset_id_{ target_asset_id }
{
}

void parser::Parser::start() {
	thread_ = std::jthread(&parser::Parser::run, this);
}

void parser::Parser::run() {
	try {
		market::MessageBuffer* message{};
		while (running_) {
			if (!network_queue_.try_dequeue(message)) {
				_mm_pause(); // spin wait
				continue;
			}

			market::MarketAccumulation* buffer{ parser_buffer_pool_.acquire() };

			if (!buffer) {
				std::cerr << "CRITICAL: Out of parser buffers, dropping!\n";
				return;
			}

			if (parseDataToBuffer(message, buffer) != parser::ParserReturnCode::SUCCESS) {
				parser_buffer_pool_.release(buffer);
			}

			network_buffer_pool_.release(message);
		}

		std::cout << "Stop message received, parser stopping\n";
	}
	catch (const std::exception& e) {
		std::cerr << std::format("CRITICAL: Unexpected error in parser: {}\n", e.what());
		throw;
	}
}

parser::ParserReturnCode parser::Parser::parseDataToBuffer(market::MessageBuffer * message, market::MarketAccumulation * buffer) {
	// simdjson expects a certain amount of padding for safe simd parsing
	simdjson::padded_string_view psv(
		reinterpret_cast<const char*>(message->data_),
		message->message_size_,
		NETWORK_BUFFER_SIZE
	);

	simdjson::ondemand::document doc;
	if (parser_.iterate(psv).get(doc)) {
		std::cerr << "Failed to parse JSON document, dropping\n";
		return parser::ParserReturnCode::PARSE_ERROR;
	}

	simdjson::ondemand::json_type type;
	if (doc.type().get(type)) {
		std::cerr << "Failed to get document type, dropping\n";
		return parser::ParserReturnCode::PARSE_ERROR;
	}

	simdjson::ondemand::object root;
	if (type == simdjson::ondemand::json_type::array) {
		// there are some weird edge cases when the entire object is wrapped in an array
		simdjson::ondemand::array arr;
		if (doc.get_array().get(arr)) {
			return parser::ParserReturnCode::PARSE_ERROR;
		}

		simdjson::ondemand::value first_element;
		if (arr.at(0).get(first_element)) {
			return parser::ParserReturnCode::PARSE_ERROR;
		}

		if (first_element.get_object().get(root)) {
			return parser::ParserReturnCode::PARSE_ERROR;
		}
	}
	else if (doc.get_object().get(root)) {
		return parser::ParserReturnCode::PARSE_ERROR;
	}

	// reset the state between parses
	buffer->type_ = market::EventType::UNKNOWN;
	buffer->asset_id_.clear();
	buffer->snapshot_asks_.fill(0);
	buffer->snapshot_bids_.fill(0);
	buffer->price_change_deltas_.clear();

	// TODO: find a better way to reset these fields to avoid trades from polluting each other
	// in theory this should never happen
	trade_accumulator_ = market::Trade{};
	bool is_trade_event{ false };

	// speculatively build/prepare the schemas to be sent to the order book 
	for (auto field : root) {
		std::string_view key;
		if (field.unescaped_key().get(key)) {
			return parser::ParserReturnCode::PARSE_ERROR;
		}

		simdjson::ondemand::value val;
		if (field.value().get(val)) {
			return parser::ParserReturnCode::PARSE_ERROR;
		}

		if (key == "event_type") {
			std::string_view event_type_str;
			if (val.get_string().get(event_type_str)) {
				continue;
			}
			buffer->type_ = market::stringToEventType(event_type_str);
		}
		else if (key == "asset_id") {
			std::string asset_id_str;
			if (val.get_string().get(asset_id_str)) {
				continue;
			}
			buffer->asset_id_ = asset_id_str;
		}
		else if (key == "timestamp") {
			std::string_view raw_timestamp;
			if (val.get_string().get(raw_timestamp)) {
				continue;
			}
			uint64_t timestamp{};
			auto [ptr, ec] = std::from_chars(raw_timestamp.data(), raw_timestamp.data() + raw_timestamp.size(), timestamp);

			if (ec == std::errc()) {
				buffer->timestamp_ = timestamp;
				trade_accumulator_.timestamp_ = timestamp;
			}
		}
		// event_type = book
		else if (key == "bids" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array bids_arr;
			if (val.get_array().get(bids_arr)) {
				continue;
			}
			getBids(bids_arr, buffer);
		}
		else if (key == "asks" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array asks_arr;
			if (val.get_array().get(asks_arr)) {
				continue;
			}
			getAsks(asks_arr, buffer);
		}
		// event_type = price_change
		else if (key == "price_changes" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array price_change_arr;
			if (val.get_array().get(price_change_arr)) {
				continue;
			}
			getPriceChanges(price_change_arr, buffer);
		}
		// event_type = last_trade_price
		else if (key == "side") {
			std::string_view trade_side;
			if (val.get_string().get(trade_side)) {
				continue;
			}
			trade_accumulator_.side_ = (trade_side == "BUY") ? market::Side::BUY : market::Side::SELL;
			is_trade_event = true;
		}
		else if (key == "size") {
			std::string_view raw_trade_size;
			if (val.get_string().get(raw_trade_size)) {
				continue;
			}
			double trade_size{};
			auto [ptr, ec] = std::from_chars(raw_trade_size.data(), raw_trade_size.data() + raw_trade_size.size(), trade_size);

			if (ec == std::errc()) {
				trade_accumulator_.size_ = trade_size;
			}
			is_trade_event = true;
		}
		else if (key == "price") {
			std::string_view raw_trade_price;
			if (val.get_string().get(raw_trade_price)) {
				continue;
			}
			double trade_price{};
			auto [ptr, ec] = std::from_chars(raw_trade_price.data(), raw_trade_price.data() + raw_trade_price.size(), trade_price);
			trade_price *= 100; // cents

			if (ec == std::errc()) {
				trade_accumulator_.price_ = static_cast<int>(trade_price);
			}
			is_trade_event = true;
		}
	}

	if (is_trade_event) {
		trade_queue_.emplace(trade_accumulator_);
	}

	if (buffer->type_ == market::EventType::UNKNOWN || buffer->asset_id_ != target_asset_id_) {
		return parser::ParserReturnCode::SEMANTIC_ERROR;
	}

	engine_queue_.enqueue(buffer);

	return parser::ParserReturnCode::SUCCESS;
}

void parser::Parser::getBids(simdjson::ondemand::array bids_arr, market::MarketAccumulation * buffer) {
	for (auto bid_result : bids_arr) {
		simdjson::ondemand::object bid;
		if (bid_result.get(bid)) {
			continue;
		}

		std::string_view raw_price;
		if (bid["price"].get_string().get(raw_price)) {
			continue;
		}

		double bid_price{};
		auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), bid_price);
		bid_price *= 100; // in cents

		std::string_view raw_size;
		if (bid["size"].get_string().get(raw_size)) {
			continue;
		}

		double bid_size{};
		auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), bid_size);

		if (ec1 == std::errc() && ec2 == std::errc()) {
			buffer->snapshot_bids_[static_cast<int>(bid_price)] = bid_size;
		}
	}
}

void parser::Parser::getAsks(simdjson::ondemand::array asks_arr, market::MarketAccumulation * buffer) {
	for (auto ask_result : asks_arr) {
		simdjson::ondemand::object ask;
		if (ask_result.get(ask)) {
			continue;
		}

		std::string_view raw_price;
		if (ask["price"].get_string().get(raw_price)) {
			continue;
		}

		double ask_price{};
		auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), ask_price);
		ask_price *= 100; // in cents

		std::string_view raw_size;
		if (ask["size"].get_string().get(raw_size)) {
			continue;
		}

		double ask_size{};
		auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), ask_size);

		if (ec1 == std::errc() && ec2 == std::errc()) {
			buffer->snapshot_asks_[static_cast<int>(ask_price)] = ask_size;
		}
	}
}

void parser::Parser::getPriceChanges(simdjson::ondemand::array price_change_arr, market::MarketAccumulation * buffer) {
	for (auto price_change_result : price_change_arr) {
		simdjson::ondemand::object price_change;
		if (price_change_result.get(price_change)) {
			continue;
		}

		std::string_view raw_asset_id;
		if (price_change["asset_id"].get_string().get(raw_asset_id)) {
			continue;
		}

		if (raw_asset_id != target_asset_id_) {
			continue;
		}

		std::string_view raw_price;
		if (price_change["price"].get_string().get(raw_price)) {
			continue;
		}

		double price{};
		auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), price);
		price *= 100; // in cents

		std::string_view raw_size;
		if (price_change["size"].get_string().get(raw_size)) {
			continue;
		}

		double size{};
		auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), size);

		std::string_view raw_side;
		if (price_change["side"].get_string().get(raw_side)) {
			continue;
		}
		market::Side side = (raw_side == "BUY") ? market::Side::BUY : market::Side::SELL;

		std::string_view raw_best_bid;
		if (price_change["best_bid"].get_string().get(raw_best_bid)) {
			continue;
		}

		double best_bid{};
		auto [ptr3, ec3] = std::from_chars(raw_best_bid.data(), raw_best_bid.data() + raw_best_bid.size(), best_bid);
		best_bid *= 100; // in cents

		std::string_view raw_best_ask;
		if (price_change["best_ask"].get_string().get(raw_best_ask)) {
			continue;
		}

		double best_ask{};
		auto [ptr4, ec4] = std::from_chars(raw_best_ask.data(), raw_best_ask.data() + raw_best_ask.size(), best_ask);
		best_ask *= 100; // in cents

		buffer->price_change_deltas_.emplace_back(
			market::OrderLevelDelta{
				.price_ = static_cast<int>(price),
				.size_ = size,
				.side_ = side,
				.best_bid_ = static_cast<int>(best_bid),
				.best_ask_ = static_cast<int>(best_ask)
			}
		);
	}
}
