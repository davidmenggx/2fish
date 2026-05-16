#pragma once

#include "component.hpp"
#include "engine/engine.hpp"

#include <array>
#include <string>

class OrderbookLevels : public Component {
public:
  OrderbookLevels(Engine &engine);

  void draw() override;

  std::string getId() override { return "Orderbook Levels"; }

private:
  void fetchData();

  Engine &engine_;

  std::array<long double, 101> yes_dollars_{};
  std::array<long double, 101> no_dollars_{};

  bool initial_scroll_{true};
};
