#include <rex/ppc.h>
#include <rex/chrono/clock.h>
#include <rex/logging.h>
#include <time.h>

namespace {
uint64_t CpuNanoseconds() {
  timespec time{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0) return 0;
  return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
}
struct Sample {
  uint64_t last = 0, last_cpu = 0, frames = 0;
  double wall = 0, cpu = 0, swap = 0;
};
thread_local Sample sample;
}  // namespace

// Preserve the original HLE implementation and PPC context. Count the calling
// thread's CPU time between frame-producing calls, including native guest code.
extern "C" void __real___imp__VdSwap(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  const auto begin = rex::chrono::Clock::QueryHostTickCount();
  __real___imp__VdSwap(ctx, base);
  const auto end = rex::chrono::Clock::QueryHostTickCount();
  const auto cpu = CpuNanoseconds();
  auto& s = sample;
  if (s.last) {
    s.wall += double(end - s.last) / 1e6;
    if (cpu >= s.last_cpu) s.cpu += double(cpu - s.last_cpu) / 1e6;
    s.swap += double(end - begin) / 1e6;
    ++s.frames;
    if (s.wall >= 2000) {
      REXLOG_INFO("WOS guest perf: {:.2f} swaps/s, {:.2f} ms/frame, "
                  "calling-thread CPU {:.2f} ms, VdSwap {:.2f} ms",
                  double(s.frames) * 1000 / s.wall, s.wall / s.frames,
                  s.cpu / s.frames, s.swap / s.frames);
      s.wall = s.cpu = s.swap = 0;
      s.frames = 0;
    }
  }
  s.last = end;
  s.last_cpu = cpu;
}
