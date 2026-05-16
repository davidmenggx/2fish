#include "orderbook_levels.hpp"
#include "common/core/types.hpp"
#include "engine/engine.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "implot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

static std::string formatWithCommas(long double value) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.3Lf", value);
  std::string str(buf);

  std::size_t dot_pos{str.find('.')};
  if (dot_pos == std::string::npos)
    dot_pos = str.length();

  int insert_count{0};
  for (int i{static_cast<int>(dot_pos) - 3}; i > 0; i -= 3) {
    if (str[i - 1] == '-')
      break;

    str.insert(i, ",");
  }
  return str;
}

OrderbookLevels::OrderbookLevels(Engine &engine) : engine_{engine} {}

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
    long double max_vol{0.0};
    int top_yes_price{-1};

    for (int p{0}; p <= 100; ++p) {
      if (yes_dollars_[p] > max_vol)
        max_vol = yes_dollars_[p];
      if (no_dollars_[p] > max_vol)
        max_vol = no_dollars_[p];

      // Find the highest price level with volume on the Yes side
      if (yes_dollars_[p] > 0) {
        top_yes_price = std::max(top_yes_price, p);
      }
    }

    if (max_vol == 0.0)
      max_vol = 1.0;
    if (top_yes_price == -1)
      top_yes_price = 50; // Fallback center

    if (ImGui::BeginTable("##OrderbookLevels", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV)) {

      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Yes Volume", ImGuiTableColumnFlags_WidthStretch, 2.0f);
      ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("No Volume", ImGuiTableColumnFlags_WidthStretch, 2.0f);

      ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
      for (int column = 0; column < 3; ++column) {
        ImGui::TableSetColumnIndex(column);
        if (column == 1) {
          float text_width = ImGui::CalcTextSize("Price").x;
          float cursor_x = ImGui::GetCursorPosX();
          float col_width = ImGui::GetColumnWidth();
          ImGui::TableHeader(
              "##Price");
          ImGui::SameLine();
          ImGui::SetCursorPosX(cursor_x + (col_width - text_width) * 0.5f);
          ImGui::Text("Price");
        } else {
          ImGui::TableHeader(column == 0 ? "Yes Volume" : "No Volume");
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

        float yes_ratio{static_cast<float>(yes_dollars_[p] / max_vol)};
        float no_ratio{static_cast<float>(no_dollars_[p] / max_vol)};

        if (yes_ratio > 0.0f) {
          ImVec2 yes_min(yes_end_x - (yes_ratio * yes_col_width), row_y1);
          ImVec2 yes_max(yes_end_x, row_y2 - 1.0f);
          draw_list->AddRectFilled(yes_min, yes_max,
                                   IM_COL32(76, 175, 80, 150));
        }

        if (no_ratio > 0.0f) {
          ImVec2 no_min(no_start_x, row_y1);
          ImVec2 no_max(no_start_x + (no_ratio * no_col_width), row_y2 - 1.0f);
          draw_list->AddRectFilled(no_min, no_max, IM_COL32(244, 67, 54, 150));
        }

        ImGui::TableSetColumnIndex(0);
        if (yes_dollars_[p] > 0)
          ImGui::Text("%s", formatWithCommas(yes_dollars_[p]).c_str());

        ImGui::TableSetColumnIndex(1);
        std::string price_str = std::to_string(p) + "\u00A2";
        float text_width{ImGui::CalcTextSize(price_str.c_str()).x};
        float col_width{ImGui::GetColumnWidth()};
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (col_width - text_width) * 0.5f);
        ImGui::Text("%s", price_str.c_str());

        ImGui::TableSetColumnIndex(2);
        if (no_dollars_[p] > 0)
          ImGui::Text("%s", formatWithCommas(no_dollars_[p]).c_str());
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
}
