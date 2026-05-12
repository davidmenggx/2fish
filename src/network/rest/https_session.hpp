#pragma once

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <memory>
#include <string>

class HttpsSession : public std::enable_shared_from_this<HttpsSession> {
public:
  HttpsSession(boost::asio::io_context &ioc, boost::asio::ssl::context &ctx);

  void run(const std::string &host, const std::string &port,
           const std::string &target, int version = 11);

private:
  void onResolve(boost::beast::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type results);
  void onConnect(boost::beast::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type::endpoint_type);
  void onHandshake(boost::beast::error_code ec);
  void onWrite(boost::beast::error_code ec, std::size_t bytes_transferred);
  void onRead(boost::beast::error_code ec, std::size_t bytes_transferred);
  void onShutdown(boost::beast::error_code ec);
  void fail(boost::beast::error_code ec, char const *what);

  boost::asio::ip::tcp::resolver resolver_;
  boost::beast::ssl_stream<boost::beast::tcp_stream> stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::empty_body> req_;
  boost::beast::http::response<boost::beast::http::string_body> res_;
};
