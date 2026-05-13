#pragma once

#include <string_view>

// For string doubles
inline double parseJsonDouble(std::string_view sv) {
  double value{0.0};
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
  if (ec != std::errc()) {
    std::cerr << "Warning: Failed to parse double from string: " << sv << '\n';
  }
  return value;
}
