#include "https_session.hpp"
#include "network/auth/generate_headers.hpp"

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

HttpsSession::HttpsSession(net::io_context &ioc, boost::asio::ssl::context &ctx)
    : resolver_(net::make_strand(ioc)), stream_(net::make_strand(ioc), ctx) {}

void HttpsSession::run(const std::string &host, const std::string &port,
                       const std::string &target,
                       std::function<void(unsigned int, std::string)> callback,
                       int version) {
  callback_ = std::move(callback);
  req_.clear();

  req_.version(version);
  req_.method(boost::beast::http::verb::get);
  req_.target(target);
  req_.set(boost::beast::http::field::host, host);
  req_.set(boost::beast::http::field::user_agent, "2fish");

  req_.keep_alive(true);

  const char *api_key_env = std::getenv("3FISH_KALSHI_API_KEY");
  if (!api_key_env)
    throw std::runtime_error("CRITICAL: API Key environment variable not set");

  std::string signing_ts{getSigningTimestampMs()};
  std::string rest_msg_to_sign{signing_ts + "GET" + target};
  const char *api_key_path = std::getenv("3FISH_KALSHI_API_PRIVATE_KEY_PATH");
  if (!api_key_path)
    throw std::runtime_error(
        "CRITICAL: API Key Path environment variable not set");

  std::string rest_signature{
      generateKalshiSignature(rest_msg_to_sign, api_key_path)};

  req_.set("KALSHI-ACCESS-KEY", api_key_env);
  req_.set("KALSHI-ACCESS-SIGNATURE", rest_signature);
  req_.set("KALSHI-ACCESS-TIMESTAMP", signing_ts);

  // Connection already established, skip
  if (is_connected_ && host == connected_host_) {
    boost::beast::http::async_write(
        stream_, req_,
        boost::beast::bind_front_handler(&HttpsSession::onWrite,
                                         shared_from_this()));
    return;
  }

  // Re-resolve connection if needed
  is_connected_ = false;
  connected_host_ = host;

  if (!SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str())) {
    boost::beast::error_code ec{static_cast<int>(::ERR_get_error()),
                                boost::asio::error::get_ssl_category()};
    std::cerr << "SNI setup failed: " << ec.message() << "\n";
    return;
  }

  resolver_.async_resolve(host, port,
                          boost::beast::bind_front_handler(
                              &HttpsSession::onResolve, shared_from_this()));
}

void HttpsSession::onResolve(beast::error_code ec,
                             tcp::resolver::results_type results) {
  if (ec)
    return fail(ec, "resolve");
  beast::get_lowest_layer(stream_).async_connect(
      results,
      beast::bind_front_handler(&HttpsSession::onConnect, shared_from_this()));
}

void HttpsSession::onConnect(beast::error_code ec,
                             tcp::resolver::results_type::endpoint_type) {
  if (ec)
    return fail(ec, "connect");
  stream_.async_handshake(boost::asio::ssl::stream_base::client,
                          beast::bind_front_handler(&HttpsSession::onHandshake,
                                                    shared_from_this()));
}

void HttpsSession::onHandshake(beast::error_code ec) {
  if (ec)
    return fail(ec, "handshake");

  is_connected_ = true;

  http::async_write(
      stream_, req_,
      beast::bind_front_handler(&HttpsSession::onWrite, shared_from_this()));
}

void HttpsSession::onWrite(beast::error_code ec,
                           std::size_t bytes_transferred) {
  if (ec)
    return fail(ec, "write");
  http::async_read(
      stream_, buffer_, res_,
      beast::bind_front_handler(&HttpsSession::onRead, shared_from_this()));
}

void HttpsSession::onRead(beast::error_code ec, std::size_t bytes_transferred) {
  if (ec)
    return fail(ec, "read");

  if (callback_)
    callback_(res_.result_int(), res_.body());

  callback_ = nullptr;
  resetForReuse();
}

void HttpsSession::fail(beast::error_code ec, char const *what) {
  std::cerr << what << ": " << ec.message() << "\n";

  is_connected_ = false;
  boost::beast::error_code close_ec;
  boost::beast::get_lowest_layer(stream_).socket().close(close_ec);

  if (callback_) {
    callback_(0, "");
    callback_ = nullptr;
  }
  resetForReuse();
}

void HttpsSession::resetForReuse() {
  res_ = {};
  buffer_.consume(buffer_.size());
}
