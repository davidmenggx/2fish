#pragma once

#include "config.hpp"
#include "constants.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

class RestClient {
public:
  explicit RestClient(Config config,
                      std::size_t thread_count = constants::REST_THREAD_COUNT);
  ~RestClient();

  void get(const std::string &host, const std::string &target);

private:
  // TODO: create a parser for this
  const Config config_;

  net::io_context ioc_;
  ssl::context ctx_;

  net::executor_work_guard<net::io_context::executor_type> work_guard_;
  std::vector<std::thread> threads_;
};
