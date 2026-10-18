#include <rex/ppc.h>
#include <rex/chrono/clock.h>
#include <rex/logging.h>
#include <time.h>
#include <array>
#include "wait_trace.h"

namespace {
uint64_t CpuNanoseconds() {
  timespec time{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0) return 0;
  return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
}
struct Sample {
  uint64_t last = 0, last_cpu = 0, frames = 0;
  double wall = 0, cpu = 0, swap = 0;
  std::array<uint64_t, 6> wait_ns{}, wait_calls{};
};
thread_local Sample sample;

void MeasureGuestWait(PPCContext& ctx, uint8_t* base, size_t kind,
                      void (*original)(PPCContext&, uint8_t*)) {
#if WOS_FRAME_COST_DIAGNOSTICS
  const auto trace = wos::trace::BeginWait(kind, kind == 2 ? ctx.r3.u32 : 0,
      uint32_t(ctx.lr), kind == 2 ? ctx.r6.u32 : kind == 4 ? ctx.r5.u32 : 0,
      kind == 2 ? ctx.r5.u32 : kind == 4 ? ctx.r4.u32 : 0, base);
  const auto start = rex::chrono::Clock::QueryHostTickCount();
  original(ctx, base);
  const auto elapsed = rex::chrono::Clock::QueryHostTickCount() - start;
  sample.wait_ns[kind] += elapsed;
  ++sample.wait_calls[kind];
  wos::trace::EndWait(trace, elapsed, ctx.r3.u32);
#else
  original(ctx, base);
#endif
}
}  // namespace

// Observe only imported guest signaling APIs, preserving every original call.
#define WOS_SIGNAL_WRAPPER(name, kind) \
extern "C" void __real___imp__##name(PPCContext&, uint8_t*); \
extern "C" void __wrap___imp__##name(PPCContext& ctx, uint8_t* base) { \
  const auto object = ctx.r3.u32; const auto caller = uint32_t(ctx.lr); \
  __real___imp__##name(ctx, base); \
  if constexpr (WOS_FRAME_COST_DIAGNOSTICS) wos::trace::Signal(kind, object, caller, ctx.r3.u32); \
}
WOS_SIGNAL_WRAPPER(KeSetEvent, 0)
WOS_SIGNAL_WRAPPER(NtSetEvent, 1)
WOS_SIGNAL_WRAPPER(KeReleaseSemaphore, 2)
#undef WOS_SIGNAL_WRAPPER

extern "C" void __real___imp__KeWaitForSingleObject(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__KeWaitForSingleObject(PPCContext& ctx, uint8_t* base) {
  MeasureGuestWait(ctx, base, 0, __real___imp__KeWaitForSingleObject);
}
extern "C" void __real___imp__KeWaitForMultipleObjects(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__KeWaitForMultipleObjects(PPCContext& ctx, uint8_t* base) {
  MeasureGuestWait(ctx, base, 1, __real___imp__KeWaitForMultipleObjects);
}
extern "C" void __real___imp__NtWaitForSingleObjectEx(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__NtWaitForSingleObjectEx(PPCContext& ctx, uint8_t* base) {
  MeasureGuestWait(ctx, base, 2, __real___imp__NtWaitForSingleObjectEx);
}
extern "C" void __real___imp__NtWaitForMultipleObjectsEx(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__NtWaitForMultipleObjectsEx(PPCContext& ctx, uint8_t* base) {
  MeasureGuestWait(ctx, base, 3, __real___imp__NtWaitForMultipleObjectsEx);
}
extern "C" void __real___imp__KeDelayExecutionThread(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__KeDelayExecutionThread(PPCContext& ctx, uint8_t* base) {
  MeasureGuestWait(ctx, base, 4, __real___imp__KeDelayExecutionThread);
}
extern "C" void __real___imp__RtlEnterCriticalSection(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__RtlEnterCriticalSection(PPCContext& ctx, uint8_t* base) {
  MeasureGuestWait(ctx, base, 5, __real___imp__RtlEnterCriticalSection);
}

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
#if WOS_FRAME_COST_DIAGNOSTICS
      const double scale = 1.0 / (1000000.0 * double(s.frames));
      REXLOG_INFO("WOS guest waits ms/frame: KeSingle {:.2f}, KeMultiple {:.2f}, "
                  "NtSingle {:.2f}, NtMultiple {:.2f}, delay {:.2f}, critical-section {:.2f}",
                  s.wait_ns[0]*scale, s.wait_ns[1]*scale, s.wait_ns[2]*scale,
                  s.wait_ns[3]*scale, s.wait_ns[4]*scale, s.wait_ns[5]*scale);
      REXLOG_INFO("WOS guest wait calls/frame: KeSingle {:.1f}, KeMultiple {:.1f}, "
                  "NtSingle {:.1f}, NtMultiple {:.1f}, delay {:.1f}, critical-section {:.1f}",
                  double(s.wait_calls[0])/s.frames, double(s.wait_calls[1])/s.frames,
                  double(s.wait_calls[2])/s.frames, double(s.wait_calls[3])/s.frames,
                  double(s.wait_calls[4])/s.frames, double(s.wait_calls[5])/s.frames);
      wos::trace::ReportWaitWindow(s.frames);
#endif
      s.wait_ns = {}; s.wait_calls = {};
      s.wall = s.cpu = s.swap = 0;
      s.frames = 0;
    }
  } else {
    // Startup/import waits are outside the first complete gameplay interval.
    s.wait_ns = {}; s.wait_calls = {};
#if WOS_FRAME_COST_DIAGNOSTICS
    wos::trace::ResetWaitWindow();
#endif
  }
  s.last = end;
  s.last_cpu = cpu;
}
