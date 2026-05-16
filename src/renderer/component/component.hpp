#pragma once

#include <string>

class Component {
public:
  virtual ~Component() = default;
  virtual void draw() = 0;
  virtual std::string getId() = 0;
  bool is_open_{true};
};
