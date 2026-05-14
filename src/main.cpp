#include "config.hpp"
#include "driver/driver.hpp"

#include <exception>
#include <format>
#include <iostream>

int main() {
  Config config{};
  config.series_ticker_ = "KXNBAGAME";
  config.market_ticker_ = "KXNBAGAME-26MAY13CLEDET-DET";

  Driver driver{config};
  try {
    driver.start();
  } catch (const std::exception &e) {
    std::cerr << std::format(
        "CRITICAL: Unexpected error occurred, aborting: {}\n", e.what());
  }

  return 0;
}
