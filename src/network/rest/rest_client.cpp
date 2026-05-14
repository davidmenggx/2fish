#include "rest_client.hpp"
#include "constants.hpp"
#include "https_session.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <simdjson.h>

#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

RestClient::RestClient(
    moodycamel::ReaderWriterQueue<RestMessage> &output_data_queue,
    std::size_t thread_count)
    : parser_{output_data_queue}, ctx_(ssl::context::tlsv12_client),
      work_guard_(net::make_work_guard(ioc_)) {
  boost::system::error_code default_ec;
  ctx_.set_default_verify_paths(default_ec);

  // Fallback for Windows
  const char *ca_cert_path = std::getenv("3FISH_CACERT_PATH");
  if (ca_cert_path) {
    boost::system::error_code ec;
    ctx_.load_verify_file(ca_cert_path, ec);
    if (ec) {
      throw std::runtime_error(std::format(
          "CRITICAL: Custom CA cert could not be loaded: {}", ec.message()));
    }
  } else if (default_ec) {
    throw std::runtime_error("CRITICAL: No default CA certificates found and "
                             "3FISH_CACERT_PATH is not set.");
  }

  ctx_.set_verify_mode(ssl::verify_peer);

  threads_.reserve(thread_count);
  for (std::size_t i{0}; i < thread_count; ++i) {
    threads_.emplace_back([this] { ioc_.run(); });
  }
}

RestClient::~RestClient() {
  work_guard_.reset();
  ioc_.stop();
  for (auto &t : threads_) {
    if (t.joinable())
      t.join();
  }
}

void RestClient::get(const std::string &host, const std::string &target) {
  auto handle_response = [this](unsigned int status, std::string body) {
    if (status == 200) {
      // TODO: Try to avoid this allocation
      parser_.parseAndPush(simdjson::padded_string(body.data(), body.size()));
    }
  };

  std::make_shared<HttpsSession>(ioc_, ctx_, handle_response)
      ->run(host, "443", target);
}
