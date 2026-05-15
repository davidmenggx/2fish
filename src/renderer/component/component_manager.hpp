#pragma once

#include "component.hpp"

#include <memory>
#include <vector>

class ComponentManager {
public:
  void addComponent(std::unique_ptr<Component> component);
  void drawAll();
private:
  std::vector<std::unique_ptr<Component>> active_components_{};
};
