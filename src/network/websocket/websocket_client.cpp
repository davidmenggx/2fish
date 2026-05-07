#include "network/websocket/websocket_client.hpp"
#include "common/websocket_data_types.hpp"
#include "network/auth/websocket_headers.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <openssl/err.h>

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

WebsocketClient::WebsocketClient(
    moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
    std::atomic<bool> &running)
    : parser_{websocket_queue}, running_{running} {}

void WebsocketClient::start() {
  thread_ = std::jthread(&WebsocketClient::run, this);
}

void WebsocketClient::run() {
  try {
    const std::string websocket_host{"external-api-ws.kalshi.com"};
    const std::string websocket_port{"443"};
    const std::string websocket_target{"/trade-api/ws/v2"};

    // Kalshi websocket payload
    std::string signing_ts{getSigningTimestampMs()};
    std::string websocket_msg_to_sign{signing_ts + "GET" + websocket_target};
    std::string websocket_signature{generateKalshiSignature(
        websocket_msg_to_sign,
        std::getenv("3FISH_KALSHI_API_PRIVATE_KEY_PATH"))};

    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_default_verify_paths();

    tcp::resolver resolver{ioc};
    websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

    auto const results = resolver.resolve(websocket_host, websocket_port);
    auto endpoint = net::connect(get_lowest_layer(ws), results);

    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                  websocket_host.c_str())) {
      throw boost::system::system_error{static_cast<int>(::ERR_get_error()),
                                        boost::asio::error::get_ssl_category()};
    }

    ws.next_layer().handshake(ssl::stream_base::client);

    ws.set_option(
        websocket::stream_base::decorator([=](websocket::request_type &req) {
          req.set("KALSHI-ACCESS-KEY", std::getenv("3FISH_KALSHI_API_KEY"));
          req.set("KALSHI-ACCESS-SIGNATURE", websocket_signature);
          req.set("KALSHI-ACCESS-TIMESTAMP", signing_ts);
        }));

    std::string host_with_port{websocket_host + ":" +
                               std::to_string(endpoint.port())};
    ws.handshake(host_with_port, websocket_target);

    std::cout << std::format("Connected to Kalshi websocket at {}{}\n",
                             websocket_host, websocket_target);

    // TODO: Replace this with a Config read
    std::string market{"KXHORMUZNORM-26MAR17-B260701"};
    std::string sub_msg{std::format(
        R"({{"id": 2, "cmd": "subscribe", "params": {{"channels": ["orderbook_delta", "trade"], "market_tickers": ["{}"]}}}})",
        market)};
    ws.write(net::buffer(sub_msg));
    std::cout << std::format("Sent subscription payload: {}\n", sub_msg);

    std::string rx_buffer;
    boost::system::error_code error_code;
    while (running_.load(std::memory_order_relaxed)) {
      rx_buffer.clear();

      auto dyn_buffer = boost::asio::dynamic_buffer(rx_buffer);
      ws.read(dyn_buffer, error_code);

      if (error_code) {
        std::cerr << "CRITICAL: Read error: " << error_code.message() << '\n';
        break;
        // TODO: Try a reconnect here
      }

      if (rx_buffer.empty())
        continue;

      if (rx_buffer.capacity() <
          rx_buffer.size() + simdjson::SIMDJSON_PADDING) {
        rx_buffer.reserve(rx_buffer.size() + simdjson::SIMDJSON_PADDING);
      }

      simdjson::padded_string_view padded_data(
          rx_buffer.data(), rx_buffer.size(), rx_buffer.capacity());

      parser_.parseAndPush(padded_data);
    }
  } catch (const std::exception &e) {
    std::cerr << std::format(
        "CRITICAL: Unexpected error in websocket client: {}\n", e.what());
    throw;
  }
}
