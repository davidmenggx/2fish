#pragma once

#include <string_view>

enum class Side { Yes, No, Unknown };

inline Side parseSide(std::string_view sv) {
  if (sv == "yes")
    return Side::Yes;
  if (sv == "no")
    return Side::No;
  return Side::Unknown;
}
