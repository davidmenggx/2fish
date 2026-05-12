#include "persistent_connection.hpp"
#include "network/auth/generate_headers.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

PersistentConnection::PersistentConnection(net::io_context &ioc,
                                           ssl::context &ctx)
    : resolver_(net::make_strand(ioc)), stream_(net::make_strand(ioc), ctx) {}

void PersistentConnection::connect(const std::string &host,
                                   const std::string &port) {
  host_ = host;
  if (!SSL_set_tlsext_host_name(stream_.native_handle(), host_.c_str())) {
    return; // Handle error
  }
  resolver_.async_resolve(
      host, port,
      beast::bind_front_handler(&PersistentConnection::onResolve,
                                shared_from_this()));
}

void PersistentConnection::sendGet(
    const std::string &target,
    std::function<void(PersistentConnection *, unsigned int,
                       const std::string &)>
        callback) {

  on_complete_ = std::move(callback);

  req_.clear();
  res_.clear();

  req_.version(11);
  req_.method(http::verb::get);
  req_.target(target);
  req_.set(http::field::host, host_);
  req_.set(http::field::user_agent, "3fish");
  req_.keep_alive(true);

  // Kalshi payload
  const char *api_key_env = std::getenv("3FISH_KALSHI_API_KEY");
  if (!api_key_env) {
    throw std::runtime_error("CRITICAL: API Key environment variable not set");
  }

  std::string signing_ts{getSigningTimestampMs()};
  std::string rest_msg_to_sign{signing_ts + "GET" + target};
  const char *api_key_path = std::getenv("3FISH_KALSHI_API_PRIVATE_KEY_PATH");
  if (!api_key_path) {
    throw std::runtime_error(
        "CRITICAL: API Key Path environment variable not set");
  }
  std::string rest_signature{
      generateKalshiSignature(rest_msg_to_sign, api_key_path)};

  req_.set("KALSHI-ACCESS-KEY", api_key_env);
  req_.set("KALSHI-ACCESS-SIGNATURE", rest_signature);
  req_.set("KALSHI-ACCESS-TIMESTAMP", signing_ts);

  http::async_write(stream_, req_,
                    beast::bind_front_handler(&PersistentConnection::onWrite,
                                              shared_from_this()));
}

void PersistentConnection::onResolve(beast::error_code ec,
                                     tcp::resolver::results_type results) {
  if (ec)
    return;
  beast::get_lowest_layer(stream_).async_connect(
      results, beast::bind_front_handler(&PersistentConnection::onConnect,
                                         shared_from_this()));
}

void PersistentConnection::onConnect(
    beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec)
    return;
  stream_.async_handshake(
      ssl::stream_base::client,
      beast::bind_front_handler(&PersistentConnection::onHandshake,
                                shared_from_this()));
}

void PersistentConnection::onHandshake(beast::error_code ec) {
  if (ec)
    return;
  is_connected_ = true;
  std::cout << "Connection to " << host_ << " warmed up and ready!\n";
}

void PersistentConnection::onWrite(beast::error_code ec, std::size_t) {
  if (ec)
    return; // Handle disconnects
  http::async_read(stream_, buffer_, res_,
                   beast::bind_front_handler(&PersistentConnection::onRead,
                                             shared_from_this()));
}

void PersistentConnection::onRead(beast::error_code ec, std::size_t) {
  if (ec) {
    is_connected_ = false; // Server dropped us
    return;
  }

  if (on_complete_)
    on_complete_(this, res_.result_int(), res_.body());
}
