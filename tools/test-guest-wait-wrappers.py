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
struct PPCContext { std::array<uint64_t, 64> regs{}; };
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
'''
code += source
names = ['KeWaitForSingleObject', 'KeWaitForMultipleObjects', 'NtWaitForSingleObjectEx',
         'NtWaitForMultipleObjectsEx', 'KeDelayExecutionThread', 'RtlEnterCriticalSection', 'VdSwap']
code += r'''
PPCContext* expected_context;
uint8_t* expected_base;
std::array<unsigned, 7> forwarded{};
void Original(PPCContext& ctx, uint8_t* base, size_t kind) {
  assert(&ctx == expected_context && base == expected_base);
  ++forwarded[kind]; ctx.regs[kind] ^= 0x1000 + kind; ++base[kind];
  tick += 500000 * (kind + 1); cpu_tick += 1000;
}
'''
for kind, name in enumerate(names):
    code += f'extern "C" void __real___imp__{name}(PPCContext& ctx, uint8_t* base) {{ Original(ctx, base, {kind}); }}\n'
code += 'using Fn = void (*)(PPCContext&, uint8_t*);\n'
code += 'std::array<Fn, 6> waits{' + ','.join('__wrap___imp__' + name for name in names[:6]) + '};\n'
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
#if WOS_FRAME_COST_DIAGNOSTICS
  assert(clock_calls == 12);
#else
  assert(clock_calls == 0);
#endif
  __wrap___imp__VdSwap(ctx, memory.data());
  for (auto count : sample.wait_calls) assert(count == 0); // discard startup waits
  for (auto fn : waits) fn(ctx, memory.data());
  tick += 2000000000;
  __wrap___imp__VdSwap(ctx, memory.data());
#if WOS_FRAME_COST_DIAGNOSTICS
  assert(logs.size() == 3);
  for (size_t kind = 0; kind < waits.size(); ++kind) {
    assert(std::abs(logs[1][kind] - 0.5 * (kind + 1)) < 1e-12);
    assert(logs[2][kind] == 1.0);
  }
#else
  assert(logs.size() == 1);
#endif
  for (auto count : sample.wait_calls) assert(count == 0);
  for (auto ns : sample.wait_ns) assert(ns == 0);
  for (auto count : forwarded) assert(count == 2);
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'guest-wait-wrappers-test.cpp'
path.write_text(code)
for enabled in [0, 1]:
    binary = args.work_dir / f'guest-wait-wrappers-test-{enabled}'
    subprocess.run([args.cxx, '-std=c++23', '-O2', '-fsanitize=address,undefined',
                    f'-DWOS_FRAME_COST_DIAGNOSTICS={enabled}', str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: all seven HLE calls forward exactly once with context/memory intact; diagnostic off/on; startup/report reset')
