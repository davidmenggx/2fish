#include "common/utils/format_with_commas.hpp"
#include "orderbook_levels.hpp"
#include "common/core/types.hpp"
#include "engine/engine.hpp"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

OrderbookLevels::OrderbookLevels(Engine &engine)
    : engine_{engine},
      orderbook_levels_buttons_{
          {{"Switch Views",
            [this] {
              is_stacked_view_ = !is_stacked_view_;
              initial_scroll_ = true;
            }},
           {"Recenter Orderbook", [this] { initial_scroll_ = true; }}}} {
  yes_dollar_diff_.fill(-1);
  no_dollar_diff_.fill(-1);
}

void OrderbookLevels::fetchData() {
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

void OrderbookLevels::draw() {
  if (!is_open_)
    return;

  fetchData();

  if (ImGui::Begin(getId().c_str(), &is_open_)) {
    long double max_volume{0.0};
    int top_yes_price{-1};

    for (int p{0}; p <= 100; ++p) {
      if (yes_dollars_[p] > max_volume)
        max_volume = yes_dollars_[p];
      if (no_dollars_[p] > max_volume)
        max_volume = no_dollars_[p];

      // Find the highest price level with volume on the Yes side
      if (yes_dollars_[p] > 0) {
        top_yes_price = std::max(top_yes_price, p);
      }
    }

    if (max_volume == 0.0)
      max_volume = 1.0;
    if (top_yes_price == -1)
      top_yes_price = 50; // Fallback center

    if (is_stacked_view_)
      drawStackedView(max_volume, top_yes_price);
    else
      drawHorizontalView(max_volume, top_yes_price);

    orderbook_levels_buttons_.drawAll();
  }
  ImGui::End();

  // Update the diffs to show up in a brighter color for contrast.
  std::copy(yes_dollars_.begin(), yes_dollars_.end(), yes_dollar_diff_.begin());
  std::copy(no_dollars_.begin(), no_dollars_.end(), no_dollar_diff_.begin());
}

void OrderbookLevels::drawStackedView(long double max_volume,
                                      int top_yes_price) {
  if (ImGui::BeginTable("##OrderbookLevels", 3, ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##Side", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch, 1.0f);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int column{0}; column < 3; ++column) {
      ImGui::TableSetColumnIndex(column);
      if (column == 1) {
        float text_width{ImGui::CalcTextSize("Price").x};
        float cursor_x{ImGui::GetCursorPosX()};
        float col_width{ImGui::GetColumnWidth()};
        ImGui::TableHeader("##Price");
        ImGui::SameLine();
        ImGui::SetCursorPosX(cursor_x + (col_width - text_width) * 0.5f);
        ImGui::Text("Price");
      } else if (column == 2) {
        float text_width{ImGui::CalcTextSize("Volume").x};
        float cursor_x{ImGui::GetCursorPosX()};
        float col_width{ImGui::GetColumnWidth()};
        ImGui::TableHeader("##Volume");
        ImGui::SameLine();
        ImGui::SetCursorPosX(cursor_x + (col_width - text_width));
        ImGui::Text("Volume");
      } else {
        ImGui::TableHeader("##Side");
      }
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();

    int lowest_ask_price{-1};
    for (int p{0}; p <= 100; ++p) {
      if (no_dollars_[100 - p] > 0) {
        lowest_ask_price = p;
        break;
      }
    }

    int highest_bid_price{-1};
    for (int p{100}; p >= 0; --p) {
      if (yes_dollars_[p] > 0) {
        highest_bid_price = p;
        break;
      }
    }

    for (int p{100}; p >= 0; --p) {
      long double ask_vol = no_dollars_[100 - p];
      long double bid_vol = yes_dollars_[p];

      ImGui::TableNextRow();

      if (initial_scroll_ && p == top_yes_price) {
        ImGui::SetScrollHereY(0.5f);
        initial_scroll_ = false;
      }

      ImGui::TableSetColumnIndex(0);
      float pad_y{ImGui::GetStyle().CellPadding.y};
      float row_y1{ImGui::GetCursorScreenPos().y - pad_y};
      float row_y2{row_y1 + ImGui::GetTextLineHeight() + (pad_y * 2.0f)};
      float bar_start_x{ImGui::GetCursorScreenPos().x};
      float table_width{ImGui::GetWindowWidth()};

      if (ask_vol > 0) {
        float ratio{static_cast<float>(ask_vol / max_volume)};
        ImVec2 bar_min(bar_start_x, row_y1);
        ImVec2 bar_max(bar_start_x + (ratio * table_width * 0.8f),
                       row_y2 - 1.0f);
        draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(255, 91, 122, 45));

        if (p == lowest_ask_price) {
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 91, 122, 255));
          ImGui::Text("Asks");
          ImGui::PopStyleColor();
        }
      } else if (bid_vol > 0) {
        float ratio{static_cast<float>(bid_vol / max_volume)};
        ImVec2 bar_min(bar_start_x, row_y1);
        ImVec2 bar_max(bar_start_x + (ratio * table_width * 0.8f),
                       row_y2 - 1.0f);
        draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(0, 200, 129, 45));

        if (p == highest_bid_price) {
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 200, 129, 255));
          ImGui::Text("Bids");
          ImGui::PopStyleColor();
        }
      }

      ImGui::TableSetColumnIndex(1);
      std::string price_str = std::to_string(p) + "\u00A2";
      float text_width{ImGui::CalcTextSize(price_str.c_str()).x};
      float col_width{ImGui::GetColumnWidth()};
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                           (col_width - text_width) * 0.5f);

      if (ask_vol > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 91, 122, 255));
        ImGui::Text("%s", price_str.c_str());
        ImGui::PopStyleColor();
      } else if (bid_vol > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 200, 129, 255));
        ImGui::Text("%s", price_str.c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::Text("%s", price_str.c_str());
        ImGui::PopStyleColor();
      }

      ImGui::TableSetColumnIndex(2);
      if (ask_vol > 0) {
        applyUpdateHighlights(p, false);
        std::string vol_str = formatWithCommas(ask_vol);
        float vol_width{ImGui::CalcTextSize(vol_str.c_str()).x};
        float vol_col_width{ImGui::GetColumnWidth()};
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (vol_col_width - vol_width));
        ImGui::Text("%s", vol_str.c_str());
        ImGui::PopStyleColor();
      } else if (bid_vol > 0) {
        applyUpdateHighlights(p, true);
        std::string vol_str = formatWithCommas(bid_vol);
        float vol_width{ImGui::CalcTextSize(vol_str.c_str()).x};
        float vol_col_width{ImGui::GetColumnWidth()};
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (vol_col_width - vol_width));
        ImGui::Text("%s", vol_str.c_str());
        ImGui::PopStyleColor();
      }
    }
    ImGui::EndTable();
  }
}

void OrderbookLevels::drawHorizontalView(long double max_volume,
                                         int top_yes_price) {
  // TODO: There are some weird gaps in the horizontal bars, check this out
  if (ImGui::BeginTable("##OrderbookLevels", 3,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                        ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Yes Bid Volume",
                            ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("No Bid Volume", ImGuiTableColumnFlags_WidthStretch,
                            3.0f);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int column{0}; column < 3; ++column) {
      ImGui::TableSetColumnIndex(column);
      if (column == 1) {
        float text_width{ImGui::CalcTextSize("Price").x};
        float cursor_x{ImGui::GetCursorPosX()};
        float col_width{ImGui::GetColumnWidth()};
        ImGui::TableHeader("##Price");
        ImGui::SameLine();
        ImGui::SetCursorPosX(cursor_x + (col_width - text_width) * 0.5f);
        ImGui::Text("Price");
      } else if (column == 2) {
        float text_width{ImGui::CalcTextSize("No Bid Volume").x};
        float cursor_x{ImGui::GetCursorPosX()};
        float col_width{ImGui::GetColumnWidth()};
        ImGui::TableHeader("##NoBidVolume");
        ImGui::SameLine();
        ImGui::SetCursorPosX(cursor_x + (col_width - text_width));
        ImGui::Text("No Bid Volume");
      } else {
        ImGui::TableHeader("Yes Bid Volume");
      }
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();

    for (int p{100}; p >= 0; --p) {
      ImGui::TableNextRow();

      if (initial_scroll_ && p == top_yes_price) {
        ImGui::SetScrollHereY(0.5f);
        initial_scroll_ = false;
      }

      ImGui::TableSetColumnIndex(0);
      float pad_y{ImGui::GetStyle().CellPadding.y};
      float row_y1{ImGui::GetCursorScreenPos().y - pad_y};
      float row_y2{row_y1 + ImGui::GetTextLineHeight() + (pad_y * 2.0f)};

      float yes_start_x{ImGui::GetCursorScreenPos().x};
      float yes_col_width{ImGui::GetColumnWidth()};
      float yes_end_x{yes_start_x + yes_col_width};

      ImGui::TableSetColumnIndex(2);
      float no_start_x{ImGui::GetCursorScreenPos().x};
      float no_col_width{ImGui::GetColumnWidth()};

      float yes_ratio{static_cast<float>(yes_dollars_[p] / max_volume)};
      float no_ratio{static_cast<float>(no_dollars_[p] / max_volume)};

      if (yes_ratio > 0.0f) {
        ImVec2 yes_min(yes_end_x - (yes_ratio * yes_col_width), row_y1);
        ImVec2 yes_max(yes_end_x, row_y2 - 1.0f);
        draw_list->AddRectFilled(yes_min, yes_max, IM_COL32(0, 200, 129, 45));
      }

      if (no_ratio > 0.0f) {
        ImVec2 no_min(no_start_x, row_y1);
        ImVec2 no_max(no_start_x + (no_ratio * no_col_width), row_y2 - 1.0f);
        draw_list->AddRectFilled(no_min, no_max, IM_COL32(255, 91, 122, 45));
      }

      ImGui::TableSetColumnIndex(0);
      if (yes_dollars_[p] > 0) {
        applyUpdateHighlights(p, true);
        ImGui::Text("%s", formatWithCommas(yes_dollars_[p]).c_str());
        ImGui::PopStyleColor();
      }

      ImGui::TableSetColumnIndex(1);
      std::string price_str = std::to_string(p) + "\u00A2";
      float text_width{ImGui::CalcTextSize(price_str.c_str()).x};
      float col_width{ImGui::GetColumnWidth()};
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                           (col_width - text_width) * 0.5f);
      ImGui::Text("%s", price_str.c_str());

      ImGui::TableSetColumnIndex(2);
      if (no_dollars_[p] > 0) {
        std::string no_vol_str = formatWithCommas(no_dollars_[p]);
        float text_width{ImGui::CalcTextSize(no_vol_str.c_str()).x};
        float col_width{ImGui::GetColumnWidth()};

        applyUpdateHighlights(p, false);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_width - text_width));
        ImGui::Text("%s", no_vol_str.c_str());
        ImGui::PopStyleColor();
      }
    }
    ImGui::EndTable();
  }
}

void OrderbookLevels::applyUpdateHighlights(int price, bool is_yes) {
  if (is_yes) {
    if (yes_dollar_diff_[price] != -1 &&
        yes_dollar_diff_[price] != yes_dollars_[price])
      yes_highlight_left_[price] = UPDATE_HIGHLIGHT_FRAMES;

    if (yes_highlight_left_[price] > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
      --yes_highlight_left_[price];
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 185));
    }
  } else {
    if (no_dollar_diff_[price] != -1 &&
        no_dollar_diff_[price] != no_dollars_[price])
      no_highlight_left_[price] = UPDATE_HIGHLIGHT_FRAMES;

    if (no_highlight_left_[price] > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
      --no_highlight_left_[price];
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 185));
    }
  }
}
