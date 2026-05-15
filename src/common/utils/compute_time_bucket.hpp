#pragma once

#include <cstdint>

inline int64_t computeTimeBucket(int64_t real_time,
                                 int64_t bucket_granularity) {
  return (real_time / bucket_granularity) * bucket_granularity;
}
