#include "config.hpp"
#include "driver/driver.hpp"

#include <exception>
#include <format>
#include <iostream>

int main() {
  Config config{};
  config.series_ticker_ = "KXBTCD15M";
  config.market_ticker_ = "KXBTC15M-26MAY141715-15";

  Driver driver{config};
  try {
    driver.start();
  } catch (const std::exception &e) {
    std::cerr << std::format(
        "CRITICAL: Unexpected error occurred, aborting: {}\n", e.what());
  }

  return 0;
}
