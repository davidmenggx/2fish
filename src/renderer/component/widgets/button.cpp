#include "button.hpp"

#include "imgui.h"

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

ButtonRow::ButtonRow(std::initializer_list<Button> buttons)
    : buttons_(buttons) {}

void ButtonRow::drawAll() {
  ImGuiStyle &style = ImGui::GetStyle();
  float total_width{0.0f};

  for (std::size_t i{0}; i < buttons_.size(); ++i) {
    float btn_width{ImGui::CalcTextSize(buttons_[i].label_.c_str()).x +
                    (style.FramePadding.x * 2.0f)};
    total_width += btn_width;

    if (i < buttons_.size() - 1)
      total_width += style.ItemSpacing.x;
  }

  float window_width{ImGui::GetWindowSize().x};
  ImGui::SetCursorPosX((window_width - total_width) * 0.5f);

  for (std::size_t i{0}; i < buttons_.size(); ++i) {
    if (ImGui::Button(buttons_[i].label_.c_str())) {
      buttons_[i].callback_();
    }

    if (i < buttons_.size() - 1)
      ImGui::SameLine();
  }
}
