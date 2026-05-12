#pragma once

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <functional>
#include <memory>
#include <string>

class PersistentConnection
    : public std::enable_shared_from_this<PersistentConnection> {
public:
  PersistentConnection(boost::asio::io_context &ioc,
                       boost::asio::ssl::context &ctx);

  void connect(const std::string &host, const std::string &port);

  bool isReady() const { return is_connected_; }

  void sendGet(const std::string &target,
                std::function<void(PersistentConnection *, unsigned int,
                                   const std::string &)>
                    callback);

private:
  void onResolve(boost::beast::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type results);
  void onConnect(boost::beast::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type::endpoint_type);
  void onHandshake(boost::beast::error_code ec);
  void onWrite(boost::beast::error_code ec, std::size_t);
  void onRead(boost::beast::error_code ec, std::size_t);

  boost::asio::ip::tcp::resolver resolver_;
  boost::beast::ssl_stream<boost::beast::tcp_stream> stream_;
  boost::beast::flat_buffer buffer_; // Reused across requests
  boost::beast::http::request<boost::beast::http::empty_body> req_;
  boost::beast::http::response<boost::beast::http::string_body> res_;

  std::string host_;
  bool is_connected_{false};

  // The callback provided by the pool manager
  std::function<void(PersistentConnection *, unsigned int, const std::string &)>
      on_complete_;
};
