#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <openssl/ssl.h>

#include <algorithm>
#include <exception>
#include <format>
#include <iostream>
#include <string>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

int main() {
	try {
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
		std::string payload{ R"({"assets_ids": ["77893140510362582253172593084218413010407941075415081594586195705930819989216"], "type": "market", "level": 3})" };

		ws.text(true);
		ws.write(net::buffer(payload));

		std::cout << std::format("Sent subscription to wss://{}{}\n\n", host, path);

		beast::flat_buffer buffer{};

		std::size_t max_size{};

		while (true) {
			ws.read(buffer);

			// for some reason std format doesn't like beast::make_printable...
			// std::cout << "Received: " << beast::make_printable(buffer.data()) << "\n\n";

			max_size = std::max(max_size, buffer.size());

			std::cout << "Max message size so far: " << max_size << '\n';

			buffer.clear();
		}
	}
	catch (const std::exception& e) {
		std::cerr << std::format("Something went wrong: {}\n", e.what());
		return 1;
	}
	return 0;
}
