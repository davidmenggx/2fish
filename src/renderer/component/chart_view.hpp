#pragma once

#include "component.hpp"
#include "constants.hpp"
#include "engine/engine.hpp"

#include <array>
#include <cstdint>
#include <string>

struct ChartViewCandlestick {
  uint8_t open_{255};
  uint8_t high_{255};
  uint8_t low_{255};
  uint8_t close_{255};
  int64_t start_timestamp_ms_{};
  bool is_loaded_{false};
};

class ChartView : public Component {
public:
  ChartView(Engine &engine);

  void draw() override;

  std::string getId() override { return "Chart View"; }

private:
  void fetchData();

  Engine &engine_;

  std::array<ChartViewCandlestick, constants::CANDLESTICK_HISTORY_STEPS>
      yes_candlesticks_{};

  std::array<ChartViewCandlestick, constants::CANDLESTICK_HISTORY_STEPS>
      no_candlesticks_{};

  enum class CandlestickWidth {Minute, Fifteen};
  CandlestickWidth candlestick_width_{CandlestickWidth::Minute};
};
