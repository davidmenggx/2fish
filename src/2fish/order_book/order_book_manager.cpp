#include "2fish/models/event_type.h"
#include "2fish/network/network_buffer_pool.h"
#include "2fish/order_book/order_book_manager.h"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <atomic>
#include <thread>

#include <format>
#include <iostream>
#include <string_view>

#include <intrin0.inl.h>

market::OrderBookManager::OrderBookManager(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue,
	NetworkBufferPool& buffer_pool, std::atomic<bool>& running)
	: market_queue_{ market_queue }, buffer_pool_{ buffer_pool }, running_{ running }
{
	price_change_buffer_.deltas_.reserve(10);
}

void market::OrderBookManager::start() {
	thread_ = std::jthread(&market::OrderBookManager::run, this);
}

void market::OrderBookManager::run() {
	market::MessageBuffer* message{};
	while (running_) {
		if (!market_queue_.try_dequeue(message)) {
			_mm_pause(); // spin wait
			continue;
		}

		std::cout << std::format("Message of {} bytes received, parsing\n", message->message_size_);

		parseAndApplyUpdate(message);

		buffer_pool_.release(message);
	}

	std::cout << "Stop message received, order book manager stopping\n";
}

void market::OrderBookManager::parseAndApplyUpdate(market::MessageBuffer* message) {
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
			std::cerr << "Array-type message contains no object, dropping!\n";
			return;
		}
		root = first_element.get_object();
	}
	else {
		root = doc.get_object();
	}

	for (auto field : root) {
		auto key_res = field.unescaped_key();

		if (key_res.error()) { 
			std::cerr << "Could not parse key in JSON field, skipping!\n";
		}

		std::string_view key{ key_res.value() };

		simdjson::ondemand::value val{ field.value() };

		if (key == "event_type") {
			event_type_ = market::stringToEventType(val.get_string().value());
		}
	}

	std::cout << "Got event type: ";
	if (event_type_ == market::EventType::kBookSnapshot) {
		std::cout << "book" << '\n';
	}
	else if (event_type_ == market::EventType::kPriceChange) {
		std::cout << "price change" << '\n';
	}
	else {
		std::cout << "something else\n";
	}



#if 0
	simdjson::ondemand::value raw_event_type;
	auto error = doc.find_field_unordered("event_type").get(raw_event_type);

	if (error == simdjson::NO_SUCH_FIELD) {
		// TODO: figure out a long term way of handling these errors
		std::cerr << "Could not find event_type field in json payload, dropping\n";
		return;
	}
	else if (error) {
		std::cerr << "Unexpected error occured when parsing event_type: " << error << '\n';
		return;
	}

	// Unfortunately the schema for Polymarket’s websocket API is not consistent: 
	// the event_type field is not in the same position (top of message) for all 
	// message types. Sadly simdjson expects a forward only stream.
	// So we pay a small performance hit to rewind the message
	doc.rewind();

	std::string_view event_str;
	error = raw_event_type.get(event_str);

	if (error) {
		std::cerr << "Could not convert raw event type to string view, dropping\n";
		return;
	}

	market::EventType event_type{ stringToEventType(event_str) };

	std::cout << std::format("Received message of type: {}\n", event_str);

	// TODO: convert the above eventtype parsing code to a static helper function in this file
#endif
}
