#pragma once

#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <immintrin.h>
#endif

inline void cpuRelax() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
  _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__)
  __asm__ volatile("yield" ::: "memory");
#else
  std::this_thread::yield();
#endif
}
