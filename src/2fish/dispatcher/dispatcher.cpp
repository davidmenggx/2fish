#include "2fish/dispatcher/dispatcher.h"
#include "2fish/models/event_type.h"
#include "2fish/models/price_change.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <atomic>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <thread>

#include <format>
#include <iostream>
#include <string>

#include <intrin0.inl.h>

market::Dispatcher::Dispatcher(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue,
	NetworkBufferPool& buffer_pool, std::atomic<bool>& running)
	: market_queue_{ market_queue }, buffer_pool_{ buffer_pool }, running_{ running }
{
	price_change_buffer_.deltas_.reserve(10);

	std::string_view target_asset_id_raw = "77893140510362582253172593084218413010407941075415081594586195705930819989216";
	target_asset_id_hash_ = std::hash<std::string_view>{}(target_asset_id_raw);
}

void market::Dispatcher::start() {
	thread_ = std::jthread(&market::Dispatcher::run, this);
}

void market::Dispatcher::run() {
	market::MessageBuffer* message{};
	while (running_) {
		if (!market_queue_.try_dequeue(message)) {
			_mm_pause(); // spin wait
			continue;
		}

		parseAndApplyUpdates(message);

		buffer_pool_.release(message);

		// TODO: a function that hits every few iterations which results in a renderer update,
		// triggered by an internal snapshot + buffer switch
	}

	std::cout << "Stop message received, order book manager stopping\n";
}

void market::Dispatcher::parseAndApplyUpdates(market::MessageBuffer* message) {
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
	event_type_ = market::EventType::kUnknown;
	book_snapshot_buffer_.asks_.fill(0);
	book_snapshot_buffer_.bids_.fill(0);
	price_change_buffer_.deltas_.clear();

	// speculatively build/prepare the schemas to be sent to the order book 
	for (auto field : root) {
		auto raw_key = field.unescaped_key();

		if (raw_key.error()) {
			continue;
		}

		std::string_view key{ raw_key.value() };

		simdjson::ondemand::value val{ field.value() };

		if (key == "event_type") {
			event_type_ = market::stringToEventType(val.get_string().value());
		}
		else if (key == "asset_id") {
			std::string_view raw_asset_id{ val.get_string().value() };
			asset_id_ = std::hash<std::string_view>{}(raw_asset_id);
		}
		else if (key == "timestamp") {
			std::string_view raw_timestamp{ val.get_string().value() };
			uint64_t timestamp{};

			auto [ptr, ec] = std::from_chars(raw_timestamp.data(), raw_timestamp.data() + raw_timestamp.size(),
				timestamp);

			if (ec == std::errc()) {
				book_snapshot_buffer_.timestamp_ = timestamp;
				price_change_buffer_.timestamp_ = timestamp;
			}
		}
		else if (key == "bids" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array bids_arr{ val.get_array() };
			for (simdjson::ondemand::object bid : bids_arr) {
				std::string_view raw_price{ bid["price"].get_string().value() };
				float bid_price{};
				auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), bid_price);
				bid_price *= 100; // in cents

				std::string_view raw_size{ bid["size"].get_string().value() };
				long double bid_size{};
				auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), bid_size);

				if (ec1 == std::errc() && ec2 == std::errc()) {
					// success path
					book_snapshot_buffer_.asks_[static_cast<int>(bid_price)] = bid_size;
				}
			}
		}
		else if (key == "asks" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array asks_arr{ val.get_array() };
			for (simdjson::ondemand::object ask : asks_arr) {
				std::string_view raw_price{ ask["price"].get_string().value() };
				float ask_price{};
				auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), ask_price);
				ask_price *= 100; // in cents

				std::string_view raw_size{ ask["size"].get_string().value() };
				long double ask_size{};
				auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), ask_size);

				if (ec1 == std::errc() && ec2 == std::errc()) {
					// success path
					book_snapshot_buffer_.asks_[static_cast<int>(ask_price)] = ask_size;
				}
			}
		}
		else if (key == "price_changes" && val.type() == simdjson::ondemand::json_type::array) {
			simdjson::ondemand::array price_change_arr{ val.get_array() };
			for (simdjson::ondemand::object price_change : price_change_arr) {
				std::string_view raw_asset_id{ price_change["asset_id"].get_string().value() };
				if (std::hash<std::string_view>{}(raw_asset_id) != target_asset_id_hash_) {
					continue;
				}

				std::string_view raw_price{ price_change["price"].get_string().value() };
				float price{};
				auto [ptr1, ec1] = std::from_chars(raw_price.data(), raw_price.data() + raw_price.size(), price);
				price *= 100; // in cents

				std::string_view raw_size{ price_change["size"].get_string().value() };
				long double size{};
				auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), size);

				std::string_view raw_side{ price_change["side"].get_string().value() };
				market::Side side = (raw_side == "BUY") ? market::Side::kBuy : market::Side::kSell;

				std::string_view raw_best_bid{ price_change["best_bid"].get_string().value() };
				float best_bid{};
				auto [ptr3, ec3] = std::from_chars(raw_best_bid.data(), raw_best_bid.data() + raw_best_bid.size(), best_bid);
				best_bid *= 100; // in cents

				std::string_view raw_best_ask{ price_change["best_ask"].get_string().value() };
				float best_ask{};
				auto [ptr4, ec4] = std::from_chars(raw_best_ask.data(), raw_best_ask.data() + raw_best_ask.size(), best_ask);
				best_ask *= 100; // in cents

				price_change_buffer_.deltas_.emplace_back(
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

	if (asset_id_ != target_asset_id_hash_) {
		return;
	}

	switch (event_type_) {
	case EventType::kUnknown:
		std::cerr << "CRITICAL: Failed to find event_type in json payload, dropping message\n";
		return;
	case EventType::kBookSnapshot:
		book_.applySnapshot(book_snapshot_buffer_);
		break;
	case EventType::kPriceChange:
		book_.applyPriceChange(price_change_buffer_);
		break;
	}

	std::cout << std::format("Best bid: {} at {}\nBest ask: {} at {}\n\n",
		book_.getBestBidSize(),
		book_.getBestBid(),
		book_.getBestAskSize(),
		book_.getBestAsk()
	);
}
