#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

template <typename T> std::string formatWithCommas(T value, int decimal_places = 2) {
  static_assert(std::is_arithmetic_v<T>, "Type must be a numeric value");

  std::string str;

  if constexpr (std::is_floating_point_v<T>) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimal_places) << value;
    str = oss.str();
  } else {
    str = std::to_string(value);
  }

  std::size_t dot_pos{str.find('.')};
  if (dot_pos == std::string::npos) {
    dot_pos = str.length();
  }

  // Insert commas every 3 digits
  for (int i{static_cast<int>(dot_pos) - 3}; i > 0; i -= 3) {
    if (str[i - 1] == '-') {
      break;
    }
    str.insert(i, ",");
  }

  return str;
}
