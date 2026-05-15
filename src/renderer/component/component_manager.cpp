#include "component_manager.hpp"
#include "component.hpp"

#include <algorithm>
#include <memory>
#include <utility>

void ComponentManager::addComponent(std::unique_ptr<Component> component) {
  active_components_.push_back(std::move(component));
}

void ComponentManager::drawAll() {
  for (auto &component : active_components_) {
    component->draw();
  }

  active_components_.erase(
      std::remove_if(
          active_components_.begin(), active_components_.end(),
          [](const std::unique_ptr<Component> &v) { return !v->is_open_; }),
      active_components_.end());
}
