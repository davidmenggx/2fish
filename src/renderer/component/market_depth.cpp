#include "market_depth.hpp"
#include "common/core/types.hpp"
#include "common/utils/format_with_commas.hpp"
#include "engine/engine.hpp"
#include "renderer/theme.hpp"

#include "imgui.h"
#include "implot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

MarketDepth::MarketDepth(Engine &engine) : engine_{engine} {}

void MarketDepth::fetchData() {
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
  auto yes_snapshot = engine_.getOrderbookSnapshot(now_ms, Side::Yes);
  auto no_snapshot = engine_.getOrderbookSnapshot(now_ms, Side::No);

  if (yes_snapshot)
    yes_dollars_ = std::move(yes_snapshot->dollars_);
  if (no_snapshot)
    no_dollars_ = std::move(no_snapshot->dollars_);
}

void MarketDepth::draw() {
  if (!is_open_)
    return;

  fetchData();

  if (ImGui::Begin(getId().c_str(), &is_open_)) {
    if (ImPlot::BeginPlot("##MarketDepth", ImVec2(-1, -1),
                          ImPlotFlags_NoMouseText)) {

      std::vector<double> bid_x, bid_y;
      std::vector<double> ask_x, ask_y;

      std::vector<double> bid_y_at_x(101, 0.0);
      std::vector<double> ask_y_at_x(101, 0.0);

      bid_x.reserve(101);
      bid_y.reserve(101);
      ask_x.reserve(101);
      ask_y.reserve(101);

      double cum_bid{0.0};
      double max_display_vol{0.0};
      int best_bid{0};

      for (int i{100}; i >= 0; --i) {
        bid_x.push_back(i);
        bid_y.push_back(cum_bid);

        cum_bid += yes_dollars_[i];
        bid_y_at_x[i] = cum_bid;

        if (cum_bid > 0 && best_bid == 0)
          best_bid = i;

        bid_x.push_back(i);
        bid_y.push_back(cum_bid);

        max_display_vol = std::max(max_display_vol, cum_bid);
      }

      double cum_ask{0.0};
      int best_ask{100};

      for (int i{0}; i <= 100; ++i) {
        ask_x.push_back(i);
        ask_y.push_back(cum_ask);

        cum_ask += no_dollars_[100 - i];
        ask_y_at_x[i] = cum_ask; // Store for hover lookup

        if (cum_ask > 0 && best_ask == 100)
          best_ask = i;

        ask_x.push_back(i);
        ask_y.push_back(cum_ask);

        max_display_vol = std::max(max_display_vol, cum_ask);
      }

      if (max_display_vol <= 0.0) {
        max_display_vol = std::max(cum_bid, cum_ask);
        if (max_display_vol <= 0.0)
          max_display_vol = 100.0;
      }

      // Lock axes and limits to prevent scrolling
      ImPlot::SetupAxis(ImAxis_X1, "Price (cents)", ImPlotAxisFlags_Lock);
      ImPlot::SetupAxis(ImAxis_Y1, "Volume", ImPlotAxisFlags_Lock);

      ImPlot::SetupAxisLimits(ImAxis_X1, 0, 100, ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_display_vol * 1.1,
                              ImPlotCond_Always);

      ImPlot::SetupLegend(ImPlotLocation_SouthWest, ImPlotLegendFlags_None);

      ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(theme::GREEN_UNFOCUSED), 2.0f);
      ImPlot::SetNextFillStyle(ImGui::ColorConvertU32ToFloat4(theme::GREEN_UNFOCUSED));
      ImPlot::PlotShaded("Bids", bid_x.data(), bid_y.data(), bid_x.size(), 0.0);
      ImPlot::PlotLine("##BidsLine", bid_x.data(), bid_y.data(), bid_x.size());

      ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(theme::RED_UNFOCUSED), 2.0f);
      ImPlot::SetNextFillStyle(ImGui::ColorConvertU32ToFloat4(theme::RED_UNFOCUSED));
      ImPlot::PlotShaded("Asks", ask_x.data(), ask_y.data(), ask_x.size(), 0.0);
      ImPlot::PlotLine("##AsksLine", ask_x.data(), ask_y.data(), ask_x.size());

      if (ImPlot::IsPlotHovered()) {
        ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos();

        int px{std::clamp(static_cast<int>(std::round(mouse_pos.x)), 0, 100)};

        double mid_price{(best_bid + best_ask) / 2.0};
        bool is_bid_side{(px <= mid_price)};

        double py = is_bid_side ? bid_y_at_x[px] : ask_y_at_x[px];
        double px_double{static_cast<double>(px)};

        ImVec4 marker_color =
            is_bid_side ? ImGui::ColorConvertU32ToFloat4(theme::GREEN_FOCUSED)
                        : ImGui::ColorConvertU32ToFloat4(theme::RED_FOCUSED);

        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, marker_color,
                                   1.0f, ImVec4(1, 1, 1, 1));
        ImPlot::PlotScatter("##HoverDot", &px_double, &py, 1);

        ImVec4 bg_color = ImVec4(0.1f, 0.1f, 0.1f, 0.9f);
        ImPlot::Annotation(px_double, py, bg_color, ImVec2(10, -15), true,
                           "Volume: %s", formatWithCommas(py).c_str());
      }

      ImPlot::EndPlot();
    }
  }
  ImGui::End();
}
