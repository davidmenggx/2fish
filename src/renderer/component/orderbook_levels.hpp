#pragma once

#include "component.hpp"
#include "engine/engine.hpp"
#include "widgets/button.hpp"

#include <array>
#include <string>

class OrderbookLevels : public Component {
public:
  OrderbookLevels(Engine &engine);

  void draw() override;

  std::string getId() override { return "Orderbook Levels"; }

private:
  void fetchData();

  // Two different view formats: display "Yes" and "No" markets
  // horizontally or reflect the "No" market bids as asks for the
  // "Yes" market
  void drawStackedView(long double max_volume, int top_yes_price);
  void drawHorizontalView(long double max_volume, int top_yes_price);

  Engine &engine_;

  std::array<long double, 101> yes_dollars_{};
  std::array<long double, 101> no_dollars_{};

  std::array<long double, 101> yes_dollar_diff_{};
  std::array<long double, 101> no_dollar_diff_{};

  void applyUpdateHighlights(int price, bool is_yes);

  static constexpr int UPDATE_HIGHLIGHT_FRAMES{20};
  std::array<int, 101> yes_highlight_left_{};
  std::array<int, 101> no_highlight_left_{};

  ButtonRow orderbook_levels_buttons_{};

  bool is_stacked_view_{true};
  bool initial_scroll_{true}; // If true, enter the user to the top of the book
};
