#pragma once

#include "common/containers/object_pool.hpp"
#include "common/core/rest_data_types.hpp"
#include "constants.hpp"
#include "https_session.hpp"
#include "rest_parser.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "moodycamel/concurrentqueue.h"

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

class RestClient {
public:
  explicit RestClient(
      moodycamel::ConcurrentQueue<RestMessage> &output_data_queue,
      std::size_t thread_count = constants::REST_THREAD_COUNT);
  ~RestClient();

  void get(const std::string &host, const std::string &target);

private:
  net::io_context ioc_;
  ssl::context ctx_;

  RestParser parser_;

  // Thread safe connection pool
  std::unique_ptr<ObjectPool<std::shared_ptr<HttpsSession>,
                             constants::HTTPS_SESSION_POOL_SIZE>>
      connection_pool_{nullptr};

  net::executor_work_guard<net::io_context::executor_type> work_guard_;
  std::vector<std::thread> threads_;
};
