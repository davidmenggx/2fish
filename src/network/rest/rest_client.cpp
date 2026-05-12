#include "rest_client.hpp"
#include "common/core/rest_data_types.hpp"
#include "persistent_connection.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

RestClient::RestClient(
    moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue,
    std::size_t pool_size)
    : parser_{rest_patch_queue}, ctx_(ssl::context::tlsv12_client),
      work_guard_(net::make_work_guard(ioc_)) {
  ctx_.set_default_verify_paths();
  ctx_.set_verify_mode(ssl::verify_peer);

  for (size_t i{0}; i < pool_size; ++i) {
    auto conn = std::make_shared<PersistentConnection>(ioc_, ctx_);
    conn->connect("external-api.kalshi.com", "443");
    all_connections_.push_back(conn);
    idle_connections_.push(conn.get());
  }

  for (size_t i{0}; i < std::thread::hardware_concurrency(); ++i) {
    threads_.emplace_back([this] { ioc_.run(); });
  }
}

RestClient::~RestClient() {
  work_guard_.reset();
  ioc_.stop();
  for (auto &t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
  all_connections_.clear();
}

void RestClient::get(const std::string &target) {
  std::cout << "A get request has been received\n";

  PersistentConnection *conn{nullptr};

  {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    if (idle_connections_.empty()) {
      std::cerr << "Pool exhausted!\n";
      return;
    }
    conn = idle_connections_.front();
    idle_connections_.pop();
  }

  auto handle_response = [this](PersistentConnection *completed_conn,
                                unsigned int status, const std::string &data) {
    if (status == 200) {
      padded_buffer_ = simdjson::padded_string(data.data(), data.size());
      this->parser_.parseAndPush(padded_buffer_);
    }

    {
      std::lock_guard<std::mutex> lock(pool_mutex_);
      idle_connections_.push(completed_conn);
    }
  };

  conn->sendGet(target, handle_response);
}
