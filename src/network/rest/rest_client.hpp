#pragma once

#include "common/core/rest_data_types.hpp"
#include "constants.hpp"
#include "rest_parser.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "moodycamel/readerwriterqueue.h"

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

class RestClient {
public:
  explicit RestClient(
      moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue,
      std::size_t thread_count = constants::REST_THREAD_COUNT);
  ~RestClient();

  void get(const std::string &host, const std::string &target);

private:
  RestParser parser_;

  net::io_context ioc_;
  ssl::context ctx_;

  net::executor_work_guard<net::io_context::executor_type> work_guard_;
  std::vector<std::thread> threads_;
};
