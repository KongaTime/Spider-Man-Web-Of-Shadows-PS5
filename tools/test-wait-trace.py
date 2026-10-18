"""Exercise the actual bounded trace collector with fake object metadata and a deterministic clock."""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('project', type=Path)
parser.add_argument('--work-dir', type=Path, required=True)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
source = (args.project / 'src/wait_trace.cpp').read_text()
source = re.sub(r'^#include <rex/[^\n]+\n', '', source, flags=re.M)
code = r'''
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include "wait_trace.h"
uint64_t tick = 1, clock_calls = 0, resolutions = 0;
thread_local uint32_t thread_id = 17;
namespace rex::chrono {
struct Clock { static uint64_t QueryHostTickCount() { ++clock_calls; return tick; } };
}
struct LogRow { std::string format; std::vector<double> fields; };
std::vector<LogRow> logs;
template<class... T> void Log(const char* format, T... values) { logs.push_back({format, {double(values)...}}); }
#define REXLOG_INFO(...) Log(__VA_ARGS__)
namespace wos::trace {
ObjectInfo ResolveObject(uint32_t handle) { ++resolutions; return {0x83000000u | (handle & 0xffff), 2}; }
ThreadInfo CurrentThread() { return {thread_id, 0x82001000u + thread_id, 0}; }
}
'''
code += source
code += r'''
int main() {
  using namespace wos::trace;
  assert(!BeginWait(2, 1, 2, 123, 0, nullptr).active); // startup must not read the guest pointer
  ResetWaitWindow();
  std::array<uint8_t, 64> memory{};
  unsigned selected = 0;
  for (unsigned i=0; i<6400; ++i) {
    auto token = BeginWait(2, 0xf8000010, 0x82001234, 8, 0, memory.data());
    if (token.active) { ++selected; assert(token.timeout == 0 && !token.missing_timeout); }
    EndWait(token, 1500, i == 6399 ? 0 : 0x102);
  }
  assert(selected == 100 && resolutions == 1 && clock_calls == 0);
  assert(waits.rows[0].count == 100 && waits.rows[0].timeouts == 99 && waits.rows[0].success == 1);
  uint64_t negative = __builtin_bswap64(uint64_t(int64_t(-10000)));
  std::memcpy(memory.data()+8, &negative, 8);
  auto delay = BeginWait(4, 0, 0x82005678, 8, 1, memory.data());
  assert(delay.active && delay.timeout == -10000 && delay.alertable == 1);
  EndWait(delay, 2500, 0xc0);
  assert(waits.rows[1].other == 1);
  auto absent = BeginWait(4, 0, 0x82005678, 0, 0, nullptr);
  assert(absent.active && absent.missing_timeout);
  EndWait(absent, 500, 0);
  ReportWaitWindow(32);
  assert(logs.size() == 4 && logs[0].fields[11] == 100 && logs[0].fields[13] == 99);
  assert(logs[0].fields[15] == 1.5 && logs[0].fields[16] == 32);
  for (const auto& row : waits.rows) assert(row.count == 0);
  const auto before_unmatched = clock_calls;
  Signal(0, 0x9999, 1, 0);
  assert(clock_calls == before_unmatched);
  logs.clear();
  Signal(0, 0x83000010, 0x82007770, 0);
  tick += 2000000000ull;
  Signal(1, 0xf8000010, 0x82007780, 1);
  assert(logs.size() == 3 && logs[0].fields[1] == 17 && logs[1].fields[0] == 1);
  logs.clear();
  std::thread worker([] {
    thread_id = 99;
    Signal(2, 0x83000010, 0x82007790, 4);
    tick += 2000000000ull;
    Signal(2, 0x83000010, 0x82007790, 3);
  });
  worker.join();
  assert(logs.size() == 2 && logs[0].fields[1] == 99 && logs[0].fields[6] == 2);
  // Bounded per-thread rows: overflow is visible, not an allocation/unbounded map.
  ResetWaitWindow();
  for (unsigned i=0; i<40; ++i) {
    auto token = BeginWait(4, 0, 0x82000000+i, 0, 0, nullptr);
    EndWait(token, 100, 0);
  }
  assert(waits.dropped == 24);
  // Fill the global watch registry using successive windows; never more than 32 entries.
  uint64_t unwatched = 0;
  for (unsigned handle=1; handle<=48; ++handle) {
    ResetWaitWindow();
    for (unsigned i=0; i<64; ++i) {
      auto token = BeginWait(2, handle, 0x82001234, 0, 0, nullptr);
      EndWait(token, 100, 0x102);
    }
    unwatched += waits.dropped;
  }
  unsigned watch_count = 0;
  for (const auto& slot : watched) if (slot.load()) ++watch_count;
  assert(watch_count == 32 && unwatched > 0);
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'wait-trace-test.cpp'
path.write_text(code)
binary = args.work_dir / 'wait-trace-test'
subprocess.run([args.cxx, '-std=c++23', '-O2', '-pthread', '-fsanitize=address,undefined',
                '-I', str(args.project / 'src'), str(path), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS: 1/64 wait sampling, big-endian timeouts, results, startup/reset, handle/guest signal matching, worker IDs and bounded overflow')
