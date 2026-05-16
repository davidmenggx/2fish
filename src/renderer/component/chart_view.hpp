#pragma once

#include "component.hpp"

#include <string>

class ChartView : public Component {
public:
  void draw() override;
  
  std::string getId() override { return "Chart View"; }

private:
	// stuff
};
