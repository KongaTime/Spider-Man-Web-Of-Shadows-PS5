// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
// Adapted from ReXGlue src/core/clock_posix.cpp for the PS5's invariant TSC.
#include <rex/chrono/clock.h>
#include <rex/assert.h>
#include <ps5platform/kernel.h>
#include <sys/time.h>
#include <time.h>
#include <x86intrin.h>

namespace {
uint64_t ReferenceNanoseconds() {
  timespec time{};
  const int result = clock_gettime(CLOCK_MONOTONIC_PRECISE, &time);
  assert_zero(result);
  return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
}
uint64_t Counter() {
  _mm_lfence();
  return __rdtsc();
}
struct Anchor {
  uint64_t frequency = sceKernelGetTscFrequency();
  uint64_t counter = 0, nanoseconds = 0;
  Anchor() {
    const uint64_t before = Counter();
    nanoseconds = ReferenceNanoseconds();
    const uint64_t after = Counter();
    counter = before + (after - before) / 2;
  }
};
}  // namespace

namespace rex::chrono {
// Supply the complete POSIX clock translation unit in the title so the static
// archive's original clock_posix.cpp.o is not selected. Keep its nanosecond
// frequency, monotonic origin and wall-clock FILETIME semantics unchanged.
uint64_t Clock::host_tick_frequency_platform() { return 1000000000ull; }

uint64_t Clock::host_tick_count_platform() {
  static const Anchor anchor;
  const uint64_t current = Counter();
  if (!anchor.frequency || current < anchor.counter)
    return ReferenceNanoseconds();
  const uint64_t elapsed = current - anchor.counter;
  // Split seconds and remainder to avoid overflowing elapsed * 1e9.
  return anchor.nanoseconds + (elapsed / anchor.frequency) * 1000000000ull +
         ((elapsed % anchor.frequency) * 1000000000ull) / anchor.frequency;
}

uint64_t Clock::QueryHostSystemTime() {
  constexpr uint64_t epoch_seconds = ((369ull * 365 + 89) * 86400);
  timeval now{};
  const int result = gettimeofday(&now, nullptr);
  assert_zero(result);
  return (uint64_t(now.tv_sec) + epoch_seconds) * 10000000ull +
         uint64_t(now.tv_usec) * 10;
}

uint64_t Clock::QueryHostUptimeMillis() {
  return host_tick_count_platform() / 1000000ull;
}
}  // namespace rex::chrono
