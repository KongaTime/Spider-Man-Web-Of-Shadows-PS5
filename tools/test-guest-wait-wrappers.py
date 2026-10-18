"""Test the actual HLE wait wrappers' forwarding, timing and frame-window reset."""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('project', type=Path)
parser.add_argument('--work-dir', required=True, type=Path)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
source = (args.project / 'src/guest_frame_diagnostics.cpp').read_text()
source = re.sub(r'^#include <rex/[^\n]+\n', '', source, flags=re.M)
code = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <time.h>
struct Reg { uint32_t u32 = 0; };
struct PPCContext { std::array<uint64_t, 64> regs{}; Reg r3, r4, r5, r6; uint64_t lr = 0; };
uint64_t tick = 1, cpu_tick = 1, clock_calls = 0;
namespace rex::chrono {
struct Clock { static uint64_t QueryHostTickCount() { ++clock_calls; return tick; } };
}
int MockClock(clockid_t, timespec* out) {
  out->tv_sec = cpu_tick / 1000000000; out->tv_nsec = cpu_tick % 1000000000; return 0;
}
#define clock_gettime MockClock
std::vector<std::vector<double>> logs;
template<class... T> void Log(const char*, T... values) { logs.push_back({double(values)...}); }
#define REXLOG_INFO(...) Log(__VA_ARGS__)
#include "wait_trace.h"
namespace wos::trace {
unsigned reset_calls = 0, report_calls = 0, signal_calls = 0;
std::vector<std::array<uint32_t, 5>> observed_waits;
std::vector<std::array<uint32_t, 4>> observed_signals;
uint32_t observed_result = 0;
WaitToken BeginWait(size_t kind, uint32_t object, uint32_t caller,
                    uint32_t ptr, uint32_t alertable, const uint8_t*) {
  if (kind == 2 || kind == 4) observed_waits.push_back({uint32_t(kind), object, caller, ptr, alertable});
  return WaitToken{kind == 2 || kind == 4, ptr == 0, uint32_t(kind), object, caller, alertable, 0};
}
void EndWait(const WaitToken& token, uint64_t, uint32_t result) { if (token.active) observed_result = result; }
void ResetWaitWindow() { ++reset_calls; }
void ReportWaitWindow(uint64_t) { ++report_calls; }
void Signal(uint32_t kind, uint32_t object, uint32_t caller, uint32_t result) {
  ++signal_calls; observed_signals.push_back({kind, object, caller, result});
}
}
'''
code += source
names = ['KeWaitForSingleObject', 'KeWaitForMultipleObjects', 'NtWaitForSingleObjectEx',
         'NtWaitForMultipleObjectsEx', 'KeDelayExecutionThread', 'RtlEnterCriticalSection', 'VdSwap',
         'KeSetEvent', 'NtSetEvent', 'KeReleaseSemaphore']
code += r'''
PPCContext* expected_context;
uint8_t* expected_base;
std::array<unsigned, 10> forwarded{};
void Original(PPCContext& ctx, uint8_t* base, size_t kind) {
  assert(&ctx == expected_context && base == expected_base);
  ++forwarded[kind]; ctx.regs[kind] ^= 0x1000 + kind; ++base[kind];
  ctx.r3.u32 ^= uint32_t(kind + 1);
  tick += 500000 * (kind + 1); cpu_tick += 1000;
}
'''
for kind, name in enumerate(names):
    code += f'extern "C" void __real___imp__{name}(PPCContext& ctx, uint8_t* base) {{ Original(ctx, base, {kind}); }}\n'
code += 'using Fn = void (*)(PPCContext&, uint8_t*);\n'
code += 'std::array<Fn, 6> waits{' + ','.join('__wrap___imp__' + name for name in names[:6]) + '};\n'
code += 'std::array<Fn, 3> signals{' + ','.join('__wrap___imp__' + name for name in names[7:]) + '};\n'
code += r'''
int main() {
  PPCContext ctx;
  std::array<uint8_t, 64> memory{};
  expected_context = &ctx; expected_base = memory.data();
  for (size_t kind = 0; kind < waits.size(); ++kind) {
    auto expected = ctx.regs;
    expected[kind] ^= 0x1000 + kind;
    waits[kind](ctx, memory.data());
    assert(ctx.regs == expected && memory[kind] == 1 && forwarded[kind] == 1);
  }
  for (auto fn : signals) fn(ctx, memory.data());
#if WOS_FRAME_COST_DIAGNOSTICS
  assert(clock_calls == 12);
#else
  assert(clock_calls == 0);
#endif
  __wrap___imp__VdSwap(ctx, memory.data());
  for (auto count : sample.wait_calls) assert(count == 0); // discard startup waits
  for (auto fn : waits) fn(ctx, memory.data());
  for (auto fn : signals) fn(ctx, memory.data());
  tick += 2000000000;
  __wrap___imp__VdSwap(ctx, memory.data());
#if WOS_FRAME_COST_DIAGNOSTICS
  assert(logs.size() == 3);
  assert(wos::trace::reset_calls == 1 && wos::trace::report_calls == 1 && wos::trace::signal_calls == 6);
  for (size_t kind = 0; kind < waits.size(); ++kind) {
    assert(std::abs(logs[1][kind] - 0.5 * (kind + 1)) < 1e-12);
    assert(logs[2][kind] == 1.0);
  }
#else
  assert(logs.size() == 1);
  assert(wos::trace::reset_calls == 0 && wos::trace::report_calls == 0 && wos::trace::signal_calls == 0);
#endif
  for (auto count : sample.wait_calls) assert(count == 0);
  for (auto ns : sample.wait_ns) assert(ns == 0);
  for (auto count : forwarded) assert(count == 2);
  ctx.r3.u32 = 0xf8001000; ctx.r4.u32 = 7; ctx.r5.u32 = 1; ctx.r6.u32 = 40; ctx.lr = 0x82001234;
  __wrap___imp__NtWaitForSingleObjectEx(ctx, memory.data());
  assert(ctx.r3.u32 == (0xf8001000u ^ 3u));
#if WOS_FRAME_COST_DIAGNOSTICS
  assert((wos::trace::observed_waits.back() == std::array<uint32_t,5>{2,0xf8001000,0x82001234,40,1}));
  assert(wos::trace::observed_result == ctx.r3.u32);
#endif
  ctx.r3.u32 = 123; ctx.r4.u32 = 0; ctx.r5.u32 = 48;
  __wrap___imp__KeDelayExecutionThread(ctx, memory.data());
#if WOS_FRAME_COST_DIAGNOSTICS
  assert((wos::trace::observed_waits.back() == std::array<uint32_t,5>{4,0,0x82001234,48,0}));
#endif
  ctx.r3.u32 = 0x83001000;
  __wrap___imp__KeSetEvent(ctx, memory.data());
  assert(ctx.r3.u32 == (0x83001000u ^ 8u));
#if WOS_FRAME_COST_DIAGNOSTICS
  assert((wos::trace::observed_signals.back() == std::array<uint32_t,4>{0,0x83001000,0x82001234,ctx.r3.u32}));
#endif
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'guest-wait-wrappers-test.cpp'
path.write_text(code)
for enabled in [0, 1]:
    binary = args.work_dir / f'guest-wait-wrappers-test-{enabled}'
    subprocess.run([args.cxx, '-std=c++23', '-O2', '-fsanitize=address,undefined', '-I', str(args.project / 'src'),
                    f'-DWOS_FRAME_COST_DIAGNOSTICS={enabled}', str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: all ten HLE calls forward exactly once with context/memory intact; diagnostic off/on; startup/report reset')
