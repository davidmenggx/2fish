#include "2fish/network/network_buffer_pool.h"
#include "2fish/network/websocket_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <openssl/ssl.h>

#include <simdjson.h>

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

market::WebsocketClient::WebsocketClient(moodycamel::ReaderWriterQueue<MessageBuffer*>& market_queue,
	NetworkBufferPool& buffer_pool, std::atomic<bool>& running, std::string_view target_asset_id_raw)
	: market_queue_{ market_queue }, buffer_pool_{ buffer_pool }
	, running_{ running }, target_asset_id_raw_{ target_asset_id_raw}
{
}

void market::WebsocketClient::start() {
	thread_ = std::jthread(&WebsocketClient::run, this);
}

void market::WebsocketClient::run() {
	std::string host{ "ws-subscriptions-clob.polymarket.com" };
	std::string port{ "443" };
	std::string path{ "/ws/market" };

	net::io_context ioc{};
	ssl::context ctx{ ssl::context::tlsv12_client };

	// TODO: verify the end target
	ctx.set_verify_mode(ssl::verify_none);

	tcp::resolver resolver{ ioc };
	websocket::stream<ssl::stream<tcp::socket>> ws{ ioc, ctx };

	const auto results = resolver.resolve(host, port);

	auto ep = net::connect(ws.next_layer().next_layer(), results);

	if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
		throw boost::system::system_error{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() };
	}

	ws.next_layer().handshake(ssl::stream_base::client);

	host += ':' + std::to_string(ep.port());

	ws.handshake(host, path);

	std::cout << std::format("Connected to wss://{}{}\n\n", host, path);

	// hard coded for now, test market
	std::string payload{ std::format(R"({{"assets_ids": ["{}"], "type": "market", "level": 2}})", target_asset_id_raw_) };
	
	ws.text(true);
	ws.write(net::buffer(payload));

	std::cout << std::format("Sent subscription to wss://{}{}\n\n", host, path);

	beast::flat_buffer fb;

	while (running_) {
		boost::system::error_code ec;
		ws.read(fb, ec);

		if (ec) {
			throw std::runtime_error("CRITICAL: unexpected error when reading websocket, aborting!");
		}

		auto data = boost::beast::buffers_front(fb.data());
		std::size_t msg_size_bytes{ data.size() };

		if (msg_size_bytes > kMaxMessageSize) {
			std::cerr << std::format("CRITICAL: message exceeded max message size {}! Message is {} bytes, dropping!\n",
				kMaxMessageSize, msg_size_bytes);
			fb.consume(fb.size());
			continue;
		}

		market::MessageBuffer* buffer{ buffer_pool_.acquire() };
		if (!buffer) {
			std::cerr << "CRITICAL: Out of network buffers, dropping!\n";
			fb.consume(fb.size());
			continue;
		}

		std::memcpy(buffer->data_, data.data(), msg_size_bytes);
		buffer->message_size_ = static_cast<uint32_t>(msg_size_bytes);

		// simdjson expects a certain amount of padding bytes,
		// so fill in the rest of the buffer with zeros
		std::memset(buffer->data_ + msg_size_bytes, 0, simdjson::SIMDJSON_PADDING);

		fb.consume(fb.size());

		market_queue_.enqueue(buffer);

		// TODO: figure out logging
		// std::cout << std::format("Enqueued message of {} bytes\n", buffer->message_size_);
	}

	std::cout << "Stop message received, websocket client stopping\n";
}
