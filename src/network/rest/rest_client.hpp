#pragma once

#include "common/core/rest_data_types.hpp"
#include "constants.hpp"
#include "persistent_connection.hpp"
#include "rest_parser.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <simdjson.h>

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class RestClient {
public:
  explicit RestClient(
      moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue,
      std::size_t pool_size = constants::REST_POOL_SIZE);

  ~RestClient();

  void get(const std::string &target);

private:
  RestParser parser_;

  boost::asio::io_context ioc_;
  boost::asio::ssl::context ctx_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard_;
  std::vector<std::thread> threads_;

  // Persistent connection pool
  std::vector<std::shared_ptr<PersistentConnection>> all_connections_;
  std::queue<PersistentConnection *> idle_connections_;
  std::mutex pool_mutex_;

  // Reusable padded string to fix simdjson allocations
  simdjson::padded_string padded_buffer_;
};
