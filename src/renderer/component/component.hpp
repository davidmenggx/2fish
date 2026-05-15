#pragma once

class Component {
public:
  virtual ~Component() = default;
  virtual void draw() = 0;
  bool is_open_{true};
};
