#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

template <typename K, typename V>
void appendParam(std::string &buf, const std::pair<K, V> &p) {
  if (buf.find('?') == std::string::npos)
    buf += '?';
  else
    buf += '&';

  buf += p.first;
  buf += '=';

  if constexpr (std::is_arithmetic_v<V>) {
    buf += std::to_string(p.second);
  } else {
    buf += p.second;
  }
}

template <typename... Args>
void setQueryParams(std::string &url, Args &&...args) {
  (appendParam(url, std::forward<Args>(args)), ...);
}
