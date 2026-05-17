#pragma once

#include <string>
#include <string_view>

enum class Side { Yes, No, Unknown };

inline std::string sideToString(Side side) {
  if (side == Side::Yes)
    return "Yes";
  if (side == Side::No)
    return "No";
  return "Unknown";
}

inline Side parseSide(std::string_view sv) {
  if (sv == "yes")
    return Side::Yes;
  if (sv == "no")
    return Side::No;
  return Side::Unknown;
}
