#include "2fish/engine/engine.h"
#include "2fish/models/market_snapshot.h"
#include "2fish/models/event_type.h"
#include "2fish/models/price_change.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book.h"
#include "2fish/utils/triple_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <format>
#include <iostream>
#include <string>

#include <intrin0.inl.h>

market::Engine::Engine(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue, NetworkBufferPool& buffer_pool,
	TripleBuffer<MarketSnapshot>& market_snapshot_buffer, std::atomic<bool>& running, std::string target_asset_id_raw)
	: market_queue_{ market_queue }, buffer_pool_{ buffer_pool }, market_snapshot_buffer_{ market_snapshot_buffer }
	, running_{ running }, target_asset_id_raw_{ std::move(target_asset_id_raw) }
{
	price_change_buffer_accumulator_.deltas_.reserve(10);
}

void market::Engine::start() {
	thread_ = std::jthread(&market::Engine::run, this);
}

void market::Engine::run() {
	market::MessageBuffer* message{};
	int messages_since_write{};

	while (running_) {
		if (!market_queue_.try_dequeue(message)) {
			if (messages_since_write > 0) {
				std::cout << "Published snapshot\n\n";
				publishSnapshot();
				messages_since_write = 0;
			}

			_mm_pause(); // spin wait
			continue;
		}

		parseAndApplyUpdates(message);

		buffer_pool_.release(message);

		if (messages_since_write > 100) {
			// if there is a big backlog of messages (high market activity),
			// just force a write periodically (every 100 messages) so the GUI stays live
			std::cout << "Published snapshot\n\n";
			publishSnapshot();
			messages_since_write = 0;
		}

		++messages_since_write;
	}

	std::cout << "Stop message received, order book manager stopping\n";
}

void market::Engine::publishSnapshot() {
	MarketSnapshot* buffer{ market_snapshot_buffer_.getWriterBuffer() };

	std::array<long double, 101> bids{ book_.getBids() };
	long double cumulative_bid_size{};
	for (auto bid_size : bids) {
		cumulative_bid_size += bid_size;
	}

	if (cumulative_bid_size <= 0) {
		std::fill(buffer->bids_weight_.begin(), buffer->bids_weight_.end(), 0);
	}
	else {
		// normalize the bid amount as a percentage for the renderer
		long double inverse_cumulative_bid_size{ 100.0L / cumulative_bid_size };
		for (size_t i{ 0 }; i < bids.size(); ++i) {
			buffer->bids_weight_[i] = static_cast<uint8_t>(bids[i] * inverse_cumulative_bid_size + 0.5L);
		}
	}

	std::array<long double, 101> asks{ book_.getAsks() };
	long double max_ask{ *std::max_element(asks.begin(), asks.end()) };

	if (max_ask <= 0) {
		std::fill(buffer->asks_weight_.begin(), buffer->asks_weight_.end(), 0);
	}
	else {
		for (std::size_t i{ 0 }; i < asks.size(); ++i) {
			buffer->asks_weight_[i] = static_cast<uint8_t>(
				(asks[i] / max_ask) * 100.0L + 0.5L);
		}
	}

	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	buffer->last_updated_ = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

	buffer->last_message_ = last_message_;

	// save our work: switch out the triple buffer
	market_snapshot_buffer_.publishWriterBuffer();
}

void market::Engine::parseAndApplyUpdates(market::MessageBuffer* message) {
	// simdjson expects a certain amount of padding for safe simd parsing
	simdjson::padded_string_view psv(
		reinterpret_cast<const char*>(message->data_),
		message->message_size_,
		kBufferSize
	);

	simdjson::ondemand::document doc{ parser_.iterate(psv) };

	simdjson::ondemand::object root;

	auto type = doc.type().value();

	if (type == simdjson::ondemand::json_type::array) {
		// there are some weird edge cases when the entire object is wrapped in an array
		auto arr = doc.get_array();
		auto first_element = arr.at(0);

		if (first_element.error()) {
			return;
		}
		root = first_element.get_object();
	}
	else {
		root = doc.get_object();
	}

	// reset the state between parses
	event_type_accumulator_ = market::EventType::kUnknown;
	book_snapshot_buffer_accumulator_.asks_.fill(0);
	book_snapshot_buffer_accumulator_.bids_.fill(0);
	price_change_buffer_accumulator_.deltas_.clear();

	// speculatively build/prepare the schemas to be sent to the order book 
	for (auto field : root) {
		auto raw_key = field.unescaped_key();

		if (raw_key.error()) {
			continue;
		}

		std::string_view key{ raw_key.value() };

		simdjson::ondemand::value val{ field.value() };

		if (key == "event_type") {
			event_type_accumulator_ = market::stringToEventType(val.get_string().value());
		}
		else if (key == "asset_id") {
			current_asset_id_accumulator_ = val.get_string().value();
		}
		else if (key == "timestamp") {
			std::string_view raw_timestamp{ val.get_string().value() };
			uint64_t timestamp{};

			auto [ptr, ec] = std::from_chars(raw_timestamp.data(), raw_timestamp.data() + raw_timestamp.size(),
				timestamp);

			if (ec == std::errc()) {
				last_message_ = timestamp;
			}
		}
		else if (key == "bids" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array bids_arr{ val.get_array() };
			for (simdjson::ondemand::object bid : bids_arr) {
				std::string_view raw_price{ bid["price"].get_string().value() };
				double bid_price{};
				auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), bid_price);
				bid_price *= 100; // in cents

				std::string_view raw_size{ bid["size"].get_string().value() };
				long double bid_size{};
				auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), bid_size);

				if (ec1 == std::errc() && ec2 == std::errc()) {
					// success path
					book_snapshot_buffer_accumulator_.bids_[static_cast<int>(bid_price)] = bid_size;
				}
			}
		}
		else if (key == "asks" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array asks_arr{ val.get_array() };
			for (simdjson::ondemand::object ask : asks_arr) {
				std::string_view raw_price{ ask["price"].get_string().value() };
				double ask_price{};
				auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), ask_price);
				ask_price *= 100; // in cents

				std::string_view raw_size{ ask["size"].get_string().value() };
				long double ask_size{};
				auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), ask_size);

				if (ec1 == std::errc() && ec2 == std::errc()) {
					// success path
					book_snapshot_buffer_accumulator_.asks_[static_cast<int>(ask_price)] = ask_size;
				}
			}
		}
		else if (key == "price_changes" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array price_change_arr{ val.get_array() };
			for (simdjson::ondemand::object price_change : price_change_arr) {
				std::string_view raw_asset_id{ price_change["asset_id"].get_string().value() };
				if (raw_asset_id != target_asset_id_raw_) {
					continue;
				}

				std::string_view raw_price{ price_change["price"].get_string().value() };
				double price{};
				auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), price);
				price *= 100; // in cents

				std::string_view raw_size{ price_change["size"].get_string().value() };
				long double size{};
				auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), size);

				std::string_view raw_side{ price_change["side"].get_string().value() };
				market::Side side = (raw_side == "BUY") ? market::Side::kBuy : market::Side::kSell;

				std::string_view raw_best_bid{ price_change["best_bid"].get_string().value() };
				double best_bid{};
				auto [ptr3, ec3] = std::from_chars(raw_best_bid.data(), raw_best_bid.data() + raw_best_bid.size(), best_bid);
				best_bid *= 100; // in cents

				std::string_view raw_best_ask{ price_change["best_ask"].get_string().value() };
				double best_ask{};
				auto [ptr4, ec4] = std::from_chars(raw_best_ask.data(), raw_best_ask.data() + raw_best_ask.size(), best_ask);
				best_ask *= 100; // in cents

				price_change_buffer_accumulator_.deltas_.emplace_back(
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
		// TODO: for now i am just visualizing liquidity, but at some point I should
		// visualize trades as well, so we need to capture the last_trade_price message

		// ^^^^ i can create a trade ledger (later)
	}

	if (current_asset_id_accumulator_ != target_asset_id_raw_) {
		std::cout << "Unknown asset id\n";
		return;
	}

	switch (event_type_accumulator_) {
	case EventType::kUnknown:
		std::cerr << "CRITICAL: Failed to find event_type in json payload, dropping message\n";
		return;
	case EventType::kBookSnapshot:
		book_.applySnapshot(book_snapshot_buffer_accumulator_);
		break;
	case EventType::kPriceChange:
		book_.applyPriceChange(price_change_buffer_accumulator_);
		break;
	}

	std::cout << "Applied an update\n\n";
}
