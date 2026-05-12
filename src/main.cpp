#include "config.hpp"
#include "driver/driver.hpp"

#include <exception>
#include <format>
#include <iostream>

int main() {
  Config config{};
  config.market_ticker_ = "KXAMERICANIDOL-26DEC31-HAN";

  Driver driver{config};
  try {
    driver.start();
  } catch (const std::exception &e) {
    std::cerr << std::format(
        "CRITICAL: Unexpected error occurred, aborting: {}\n", e.what());
  }

  return 0;
}
