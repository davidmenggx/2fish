#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

struct Button {
  std::string label_{};
  std::function<void()> callback_{};
};

class ButtonRow {
public:
  explicit ButtonRow(std::initializer_list<Button> buttons);

  void drawAll();

private:
  std::vector<Button> buttons_{};
};
