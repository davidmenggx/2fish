#include "rest_client.hpp"
#include "constants.hpp"
#include "https_session.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

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
    moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue,
    std::size_t thread_count)
    : parser_{rest_patch_queue}, ctx_(ssl::context::tlsv12_client),
      work_guard_(net::make_work_guard(ioc_)) {
  ctx_.set_default_verify_paths();
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
  std::make_shared<HttpsSession>(ioc_, ctx_)->run(host, "443", target);
}
