"""Check the actual opt-in coarse timer scopes with a deterministic host clock."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('sdk', type=Path)
parser.add_argument('--work-dir', required=True, type=Path)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
text = (args.sdk / 'src/graphics/vulkan/command_processor.cpp').read_text()
begin = text.index('enum class WosCost {')
end = text.index('// Compare the actual Vulkan descriptor contents', begin)
code = r'''
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <time.h>
#include <vector>
bool enabled = false;
uint64_t tick = 1, clock_calls = 0;
namespace rex::chrono {
struct Clock { static uint64_t QueryHostTickCount() { ++clock_calls; return tick; } };
}
#define REX_PLATFORM_WIN32 0
#define REXCVAR_GET(...) ::enabled
std::vector<std::vector<double>> logged;
template<class... T> void Log(const char*, T... values) { logged.push_back({double(values)...}); }
#define REXLOG_INFO(...) Log(__VA_ARGS__)
'''
code += text[begin:end]
code += r'''
int main() {
  { WosCostScope cost(WosCost::kDraw); tick += 100; }
  { WosCostReport report; }
  assert(clock_calls == 0 && logged.empty());
  enabled = true;
  { WosCostReport report; } // establishes the first complete interval
  const auto interval_start = tick;
  {
    WosCostScope draw(WosCost::kDraw);
    tick += 10;
    {
      WosCostScope bindings(WosCost::kBindings);
      tick += 20;
      bindings.Stop();
      tick += 30;
    }
    tick += 40;
  }
  assert(wos_costs.ns[size_t(WosCost::kDraw)] == 100);
  assert(wos_costs.ns[size_t(WosCost::kBindings)] == 20);
  assert(wos_costs.calls[size_t(WosCost::kBindings)] == 1);
  // Early return still records exactly one scope completion.
  auto early = [] { WosCostScope cost(WosCost::kResidency); tick += 7; return false; };
  assert(!early());
  assert(wos_costs.ns[size_t(WosCost::kResidency)] == 7);
  tick = interval_start + 1000000000;
  { WosCostReport report; }
  assert(logged.empty());
  tick = interval_start + 2000000000;
  { WosCostReport report; }
  assert(logged.size() == 4 && logged[0][0] == 1.0 && logged[0][1] == 1000.0);
  assert(logged[0][3] == 0.5); // one draw in two reported frames
  assert(std::abs(logged[1][0] - 100.0 / 2000000.0) < 1e-12);
  assert(wos_costs.frames == 0 && wos_costs.ns[size_t(WosCost::kDraw)] == 0);
  enabled = false;
  const auto before = clock_calls;
  { WosCostScope cost(WosCost::kDraw); }
  { WosCostReport report; }
  assert(clock_calls == before && logged.size() == 4 && wos_costs.last == 0);
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'frame-costs-test.cpp'
path.write_text(code)
binary = args.work_dir / 'frame-costs-test'
subprocess.run([args.cxx, '-std=c++23', '-O2', '-fsanitize=address,undefined',
                str(path), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS: disabled path makes no clock calls; nested scopes, explicit stop, early return, reporting and reset')
