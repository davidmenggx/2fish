#include "2fish/engine/engine.h"
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

#include <exception>

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
				publishSnapshot();
				messages_since_write = 0;
			}

			++messages_since_write;
		}

		std::cout << "Stop message received, order book manager stopping\n";
}

void market::Engine::publishSnapshot() {
	MarketSnapshot* buffer{ market_snapshot_buffer_.getWriterBuffer() };

	std::array<long double, 101> book_bids{ book_.getBids() };
	std::copy(book_bids.begin(), book_bids.end(), buffer->bids_.begin());

	std::array<long double, 101> book_asks{ book_.getAsks() };
	std::copy(book_asks.begin(), book_asks.end(), buffer->asks_.begin());

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

    simdjson::ondemand::document doc;
    if (parser_.iterate(psv).get(doc)) {
        std::cerr << "CRITICAL: Failed to parse JSON document. Dropping\n";
        return;
    }

    simdjson::ondemand::json_type type;
    if (doc.type().get(type)) {
        std::cerr << "Failed to get document type\n";
        return;
    }

    simdjson::ondemand::object root;
    if (type == simdjson::ondemand::json_type::array) {
        // there are some weird edge cases when the entire object is wrapped in an array
        simdjson::ondemand::array arr;
        if (doc.get_array().get(arr)) return;

        simdjson::ondemand::value first_element;
        if (arr.at(0).get(first_element)) return;

        if (first_element.get_object().get(root)) return;
    }
    else {
        if (doc.get_object().get(root)) return;
    }

    // reset the state between parses
    event_type_accumulator_ = market::EventType::kUnknown;
    book_snapshot_buffer_accumulator_.asks_.fill(0);
    book_snapshot_buffer_accumulator_.bids_.fill(0);
    price_change_buffer_accumulator_.deltas_.clear();

    // speculatively build/prepare the schemas to be sent to the order book 
    for (auto field : root) {
        std::string_view key;
        if (field.unescaped_key().get(key)) {
            continue;
        }

        simdjson::ondemand::value val;
        if (field.value().get(val)) {
            continue;
        }

        if (key == "event_type") {
            std::string_view event_type_str;
            if (val.get_string().get(event_type_str)) {
                continue;
            }

            event_type_accumulator_ = market::stringToEventType(event_type_str);
        }
        else if (key == "asset_id") {
            std::string_view asset_id_str;
            if (val.get_string().get(asset_id_str)) {
                continue;
            }

            current_asset_id_accumulator_ = asset_id_str;
        }
        else if (key == "timestamp") {
            std::string_view raw_timestamp;
            if (val.get_string().get(raw_timestamp)) {
                continue;
            }

            uint64_t timestamp{};
            auto [ptr, ec] = std::from_chars(raw_timestamp.data(), raw_timestamp.data() + raw_timestamp.size(), timestamp);

            if (ec == std::errc()) {
                last_message_ = timestamp;
            }
        }
        else if (key == "bids" && val.type() == simdjson::ondemand::json_type::array) {
            simdjson::ondemand::array bids_arr;
            if (val.get_array().get(bids_arr)) {
                continue;
            }

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

                long double bid_size{};
                auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), bid_size);

                if (ec1 == std::errc() && ec2 == std::errc()) {
                    book_snapshot_buffer_accumulator_.bids_[static_cast<int>(bid_price)] = bid_size;
                }
            }
        }
        else if (key == "asks" && val.type() == simdjson::ondemand::json_type::array) {
            simdjson::ondemand::array asks_arr;
            if (val.get_array().get(asks_arr)) {
                continue;
            }

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

                long double ask_size{};
                auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), ask_size);

                if (ec1 == std::errc() && ec2 == std::errc()) {
                    book_snapshot_buffer_accumulator_.asks_[static_cast<int>(ask_price)] = ask_size;
                }
            }
        }
        else if (key == "price_changes" && val.type() == simdjson::ondemand::json_type::array) {
            simdjson::ondemand::array price_change_arr;
            if (val.get_array().get(price_change_arr)) {
                continue;
            }

            for (auto price_change_result : price_change_arr) {
                simdjson::ondemand::object price_change;
                if (price_change_result.get(price_change)) {
                    continue;
                }

                std::string_view raw_asset_id;
                if (price_change["asset_id"].get_string().get(raw_asset_id)) {
                    continue;
                }
                if (raw_asset_id != target_asset_id_raw_) {
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

                long double size{};
                auto [ptr2, ec2] = std::from_chars(raw_size.data(), raw_size.data() + raw_size.size(), size);

                std::string_view raw_side;
                if (price_change["side"].get_string().get(raw_side)) {
                    continue;
                }
                market::Side side = (raw_side == "BUY") ? market::Side::kBuy : market::Side::kSell;

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
}
