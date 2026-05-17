#include "trade_ledger.hpp"
#include "common/core/types.hpp"
#include "common/utils/format_with_commas.hpp"
#include "engine/engine.hpp"

#include "imgui.h"

#include <chrono>
#include <format>

TradeLedger::TradeLedger(Engine &engine) : engine_{engine} {}

void TradeLedger::fetchData() {
  auto trade{engine_.getFirstTrade()};

  if (trade) {
    TradeLedgerItem item{.yes_price_cents_ = trade->yes_price_cents_,
                         .no_price_cents_ = trade->no_price_cents_,
                         .contracts_traded_ = trade->contracts_traded_,
                         .taker_side_ = trade->taker_side_,
                         .timestamp_ms_ = trade->timestamp_ms_};

    // These are null terminated character std::arrays
    for (char c : trade->market_ticker_) {
      if (c == '\n')
        break;
      item.market_ticker_ += c;
    }

    for (char c : trade->trade_id_) {
      if (c == '\n')
        break;
      item.trade_id_ += c;
    }

    // Fixed capacity is automatically managed
    trade_ledger_.push_back(item);
  }
}

void TradeLedger::draw() {
  if (!is_open_)
    return;

  fetchData();

  if (ImGui::Begin(getId().c_str(), &is_open_)) {
    if (ImGui::BeginTable("##TradeLedger", 6,
                          ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_BordersInnerV,
                          ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Market Ticker",
                              ImGuiTableColumnFlags_WidthStretch, 1.5f);
      ImGui::TableSetupColumn("Trade ID", ImGuiTableColumnFlags_WidthStretch,
                              1.5f);
      ImGui::TableSetupColumn("Taker Side", ImGuiTableColumnFlags_WidthStretch,
                              1.0f);
      ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch,
                              1.0f);
      ImGui::TableSetupColumn("Contracts Traded",
                              ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthStretch,
                              2.0f);

      ImGui::TableHeadersRow();

      int row_index = 0;
      for (auto it = trade_ledger_.rbegin(); it != trade_ledger_.rend(); ++it) {
        ImGui::TableNextRow();
        ImGui::PushID(row_index);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(it->market_ticker_.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", it->market_ticker_.c_str());

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(it->trade_id_.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", it->trade_id_.c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%s", sideToString(it->taker_side_).c_str());

        ImGui::TableNextColumn();
        if (it->taker_side_ == Side::No)
          ImGui::Text("%i\u00A2", it->no_price_cents_);
        else
          ImGui::Text("%i\u00A2", it->yes_price_cents_);

        ImGui::TableNextColumn();
        ImGui::Text("%s", formatWithCommas(it->contracts_traded_).c_str());

        ImGui::TableNextColumn();
        std::chrono::system_clock::time_point tp{
            std::chrono::milliseconds(it->timestamp_ms_)};

        std::string formatted_time = std::format("{:%Y-%m-%d %H:%M:%S}", tp);

        ImGui::TextUnformatted(formatted_time.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", formatted_time.c_str());

        ImGui::PopID();
        row_index++;
      }

      ImGui::EndTable();
    }
  }
  ImGui::End();
}
