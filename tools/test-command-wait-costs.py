"""Validate extracted command-wait timers with deterministic wall and CPU clocks."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('sdk', type=Path)
parser.add_argument('--work-dir', required=True, type=Path)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
text = (args.sdk / 'src/graphics/command_processor.cpp').read_text()
begin = text.index('enum class WosWaitKind {')
end = text.index('ReadbackResolveMode ParseReadbackResolveMode(', begin)
code = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <time.h>
#include <vector>
bool flag_enabled = false;
uint64_t tick = 1, cpu_tick = 100, wall_calls = 0, cpu_calls = 0;
namespace rex::chrono {
struct Clock { static uint64_t QueryHostTickCount() { ++wall_calls; return tick; } };
}
int MockCpuClock(clockid_t, timespec* out) {
  ++cpu_calls; out->tv_sec = cpu_tick / 1000000000; out->tv_nsec = cpu_tick % 1000000000; return 0;
}
#define clock_gettime MockCpuClock
#define REX_PLATFORM_WIN32 0
#define REXCVAR_GET(...) ::flag_enabled
std::vector<std::vector<double>> logs;
template<class... T> void Log(const char*, T... values) { logs.push_back({double(values)...}); }
#define REXLOG_INFO(...) Log(__VA_ARGS__)
'''
code += text[begin:end]
code += r'''
int main() {
  { WosWaitScope ring(WosWaitKind::kRing); }
  WosReportCommandWaitCosts();
  assert(wall_calls == 0 && cpu_calls == 0 && logs.empty());
  flag_enabled = true;
  WosReportCommandWaitCosts();
  const auto start = tick;
  {
    WosWaitScope ring(WosWaitKind::kRing);
    tick += 10; cpu_tick += 2;
    {
      WosWaitScope packet(WosWaitKind::kPacket);
      tick += 20; cpu_tick += 3;
      { WosWaitScope coherency(WosWaitKind::kCoherency); tick += 5; cpu_tick += 1; }
    }
    tick += 15; cpu_tick += 4;
  }
  assert(wos_wait_costs.ns[0] == 50 && wos_wait_costs.ring_cpu == 10);
  assert(wos_wait_costs.ns[1] == 25 && wos_wait_costs.ns[3] == 5);
  assert(cpu_calls == 2); // only ring stalls read CPU clocks
  tick = start + 1000000000; WosReportCommandWaitCosts();
  assert(logs.empty());
  tick = start + 2000000000; WosReportCommandWaitCosts();
  assert(logs.size() == 1);
  assert(std::abs(logs[0][0] - 50.0 / 2000000.0) < 1e-12);
  assert(std::abs(logs[0][1] - 10.0 / 2000000.0) < 1e-12);
  assert(logs[0][5] == 0.5 && logs[0][6] == 0.5);
  assert(wos_wait_costs.frames == 0 && wos_wait_costs.ring_cpu == 0);
  flag_enabled = false;
  auto before_wall = wall_calls, before_cpu = cpu_calls;
  { WosWaitScope ring(WosWaitKind::kRing); }
  WosReportCommandWaitCosts();
  assert(wall_calls == before_wall && cpu_calls == before_cpu);
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'command-wait-costs-test.cpp'
path.write_text(code)
binary = args.work_dir / 'command-wait-costs-test'
subprocess.run([args.cxx, '-std=c++23', '-O2', '-fsanitize=address,undefined',
                str(path), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS: disabled/no clocks, nested wall intervals, ring CPU time, report interval and reset')
