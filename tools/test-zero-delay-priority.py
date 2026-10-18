"""Exercise the actual Delay and SetPriority methods with recorded host sleep/yield calls."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('sdk', type=Path)
parser.add_argument('--work-dir', type=Path, required=True)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
source = (args.sdk / 'src/system/xthread.cpp').read_text()

def method(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth, cursor = 1, brace + 1
    while depth:
        if source[cursor] == '{': depth += 1
        if source[cursor] == '}': depth -= 1
        cursor += 1
    return source[start:cursor]

code = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <vector>
using X_STATUS = uint32_t;
constexpr X_STATUS X_STATUS_SUCCESS = 0, X_STATUS_USER_APC = 0xc0;
unsigned termination_checks = 0, yields = 0, asserts = 0, scalar = 1;
std::vector<int64_t> sleeps, alertable_sleeps;
#define assert_always() (++asserts)
#define REXCVAR_GET(name) false
namespace chrono {
struct Clock { static uint32_t ScaleGuestDurationMillis(uint32_t ms) { return ms / scalar; } };
}
namespace rex::thread {
struct ThreadPriority {
  static constexpr int kLowest=1, kBelowNormal=8, kNormal=16, kAboveNormal=24, kHighest=32;
};
enum class SleepResult { kSuccess, kAlerted, kOther };
SleepResult sleep_result = SleepResult::kSuccess;
void Sleep(std::chrono::microseconds duration) { sleeps.push_back(duration.count()); }
void MaybeYield() { ++yields; }
SleepResult AlertableSleep(std::chrono::microseconds duration) {
  alertable_sleeps.push_back(duration.count()); return sleep_result;
}
}
struct X_KTHREAD { uint8_t priority = 0; };
struct HostThread { int value = -999; void set_priority(int priority) { value = priority; } };
struct XThread {
  int32_t priority_ = 0;
  X_KTHREAD guest;
  HostThread host;
  HostThread* thread_ = &host;
  template<class T> T* guest_object() { return &guest; }
  void CheckTitleTermination() { ++termination_checks; }
  void SetPriority(int32_t increment);
  X_STATUS Delay(uint32_t processor_mode, uint32_t alertable, uint64_t interval);
};
'''
code += '\nvoid ' + method(source, 'XThread::SetPriority(') + '\n'
code += '\nX_STATUS ' + method(source, 'XThread::Delay(') + '\n'
code += r'''
int main() {
  unsigned cases = 0;
  for (int priority : {-100,-35,-34,-18,-17,-1,0,1,8,9,17,18,34,35,100}) {
    XThread thread;
    thread.SetPriority(priority);
    assert(thread.priority_ == priority);
    assert(thread.guest.priority == std::clamp(priority, 0, 31));
    const bool low_priority = thread.host.value <= rex::thread::ThreadPriority::kBelowNormal;
    for (int64_t interval : {0ll,-1ll,-9999ll,-10000ll,-25000ll,-123000000ll}) {
      for (unsigned scale : {1u,2u}) { // also scale a nonzero delay to zero
        scalar = scale;
        for (unsigned mode = 0; mode < 4; ++mode) {
          sleeps.clear(); alertable_sleeps.clear(); termination_checks = yields = asserts = 0;
          const bool alertable = mode != 0;
          rex::thread::sleep_result = mode == 2 ? rex::thread::SleepResult::kAlerted :
              mode == 3 ? rex::thread::SleepResult::kOther : rex::thread::SleepResult::kSuccess;
          const auto result = thread.Delay(1, alertable, uint64_t(interval));
          const uint32_t ms = uint32_t(-interval / 10000) / scalar;
          assert(termination_checks == 2 && asserts == 0);
          if (alertable) {
            assert(sleeps.empty() && yields == 0);
            assert(alertable_sleeps == std::vector<int64_t>{int64_t(ms)*1000});
            assert(result == (mode == 2 ? X_STATUS_USER_APC : X_STATUS_SUCCESS));
          } else {
            assert(alertable_sleeps.empty() && result == X_STATUS_SUCCESS);
            if (ms) {
              assert(sleeps == std::vector<int64_t>{int64_t(ms)*1000} && yields == 0);
            } else if (low_priority) {
              assert(sleeps == std::vector<int64_t>{100} && yields == 0);
            } else {
              assert(sleeps.empty() && yields == 1);
            }
          }
          ++cases;
        }
      }
    }
  }
  assert(cases == 720);
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'zero-delay-priority-test.cpp'
path.write_text(code)
binary = args.work_dir / 'zero-delay-priority-test'
subprocess.run([args.cxx, '-std=c++23', '-O2', '-fsanitize=address,undefined',
                str(path), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS: 720 actual Delay/SetPriority cases: guest/host priority boundary, zero/sub-ms/timed delays, scaling, alertable results and termination checks')
