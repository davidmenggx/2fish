#include "driver.hpp"

#include "config.hpp"

Driver::Driver(Config config)
    : engine_{websocket_queue_, rest_patch_queue_, config, running_},
      websocket_client_{websocket_queue_, config, running_} {}

void Driver::start() {
  engine_.start();
  websocket_client_.start();
}
