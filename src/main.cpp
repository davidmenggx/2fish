#include "config.hpp"
#include "driver/driver.hpp"

#include <exception>
#include <format>
#include <iostream>

int main() {
  Config config{};
  config.series_ticker_ = "KXBTCD";
  config.market_ticker_ = "KXBTCD-26MAY1717-T77999.99";

  Driver driver{config};
  try {
    driver.start();
  } catch (const std::exception &e) {
    std::cerr << std::format(
        "CRITICAL: Unexpected error occurred, aborting: {}\n", e.what());
  }

  return 0;
}
