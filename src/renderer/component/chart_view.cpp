#include "chart_view.hpp"
#include "common/core/types.hpp"
#include "constants.hpp"
#include "engine/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

ChartView::ChartView(Engine &engine) : engine_{engine} {}

void ChartView::fetchData() {
  // Clear stale data
  auto set_unloaded = [](auto &candlestick) { candlestick.is_loaded_ = false; };
  std::ranges::for_each(yes_candlesticks_, set_unloaded);
  std::ranges::for_each(no_candlesticks_, set_unloaded);

  const auto now = std::chrono::system_clock::now();
  const int64_t current_query_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count();

  auto populate_candlesticks = [this, current_query_time](auto &candlestick_vec,
                                                          Side side) {
    const std::size_t size{candlestick_vec.size()};

    for (std::size_t i{0}; i < size; ++i) {
      auto lookup = engine_.getCandlestick(current_query_time, side);

      std::size_t target_idx{size - i - 1};

      if (lookup)
        candlestick_vec[target_idx] = {.open_ = lookup->open_,
                                       .high_ = lookup->high_,
                                       .low_ = lookup->low_,
                                       .close_ = lookup->close_,
                                       .start_timestamp_ms_ =
                                           lookup->start_timestamp_ms_,
                                       .is_loaded_ = true};
      else
        candlestick_vec[target_idx] = {.open_ = 0,
                                       .high_ = 0,
                                       .low_ = 0,
                                       .close_ = 0,
                                       .start_timestamp_ms_ = 0,
                                       .is_loaded_ = false};
    }
  };

  populate_candlesticks(yes_candlesticks_, Side::Yes);
  populate_candlesticks(no_candlesticks_, Side::No);
}

void ChartView::draw() {
  if (!is_open_)
    return;

  fetchData();
}
