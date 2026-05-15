#pragma once

#include <type_traits>

template <typename T> T priceRound(T value, T epsilon = 1e-6) {
  static_assert(std::is_floating_point_v<T>, "Must use a floating point type");

  return std::floor(value + epsilon);
}
