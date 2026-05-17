#include "component.hpp"
#include "engine/engine.hpp"

#include <array>
#include <string>

class MarketDepth : public Component {
public:
  MarketDepth(Engine &engine);

  void draw() override;

  std::string getId() override { return "Market Depth"; }

private:
  void fetchData();

  Engine &engine_;

  std::array<long double, 101> yes_dollars_{};
  std::array<long double, 101> no_dollars_{};
};
