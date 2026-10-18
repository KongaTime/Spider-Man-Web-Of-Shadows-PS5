"""Run the extracted command-ring worker with host queues, events and fake packets."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('sdk', type=Path)
parser.add_argument('--work-dir', required=True, type=Path)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
source = (args.sdk / 'src/graphics/command_processor.cpp').read_text()
def method(name):
    start = source.index('void CommandProcessor::' + name + '(')
    brace = source.index('{', start)
    depth, cursor = 1, brace + 1
    while depth:
        if source[cursor] == '{': depth += 1
        if source[cursor] == '}': depth -= 1
        cursor += 1
    return source[start:cursor] + '\n'

code = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
std::atomic<uint32_t> spin_limit = 16;
#define REXCVAR_GET(...) ::spin_limit.load()
#define SCOPE_profile_cpu_i(...) ((void)0)
#define assert_true(x) assert(x)
enum class WosWaitKind { kRing };
struct WosWaitScope { explicit WosWaitScope(WosWaitKind) {} };
struct Event {
  std::mutex mutex;
  std::condition_variable cv;
  bool signaled = false;
  std::atomic<unsigned> waits{0};
  void Set() { std::lock_guard lock(mutex); signaled = true; cv.notify_one(); }
  void Wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex);
    ++waits;
    if (cv.wait_for(lock, timeout, [&]{ return signaled; })) signaled = false;
  }
};
struct Queue {
  std::mutex mutex;
  std::queue<std::function<void()>> queue;
  bool empty() { std::lock_guard lock(mutex); return queue.empty(); }
  auto front() { std::lock_guard lock(mutex); return queue.front(); }
  void pop() { std::lock_guard lock(mutex); queue.pop(); }
  void push(std::function<void()> fn) { std::lock_guard lock(mutex); queue.push(std::move(fn)); }
};
namespace rex {
void FatalError(const char*) { assert(false); }
namespace thread {
void MaybeYield() { std::this_thread::yield(); }
void Wait(Event* event, bool, std::chrono::milliseconds timeout) { event->Wait(timeout); }
}
}
namespace system_test { struct XThread { static bool IsInThread(void*) { return false; } }; }
#define system system_test
namespace memory { template<class T> void store_and_swap(void*, T) {} }
struct Memory { void* TranslatePhysical(uint32_t) { return nullptr; } };
struct CommandProcessor {
  std::atomic<bool> worker_running_{true};
  Queue pending_fns_;
  std::unique_ptr<Event> write_ptr_index_event_ = std::make_unique<Event>();
  std::unique_ptr<int> worker_thread_;
  std::atomic<uint32_t> write_ptr_index_{0}, completed{0};
  uint32_t read_ptr_index_ = 0, read_ptr_writeback_ptr_ = 0;
  Memory* memory_ = nullptr;
  bool SetupContext() { return true; }
  void ShutdownContext() {}
  void PrepareForWait() {}
  void ReturnFromWait() {}
  uint32_t ExecutePrimaryBuffer(uint32_t, uint32_t write) { completed = write; return write; }
  void WorkerThreadMain();
  void CallInThread(std::function<void()> fn);
  void UpdateWritePointer(uint32_t value);
};
'''
for name in ['WorkerThreadMain', 'CallInThread', 'UpdateWritePointer']:
    code += method(name)
code += r'''
template<class F> void Until(F ready) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!ready()) { assert(std::chrono::steady_clock::now() < end); std::this_thread::yield(); }
}
int main() {
  for (uint32_t limit : {0u, 16u, 500u}) {
    spin_limit = limit;
    CommandProcessor cp;
    std::thread worker([&] { cp.WorkerThreadMain(); });
    Until([&] { return cp.write_ptr_index_event_->waits.load() > 0; });
    // A published pointer must wake a worker that already entered its wait.
    for (uint32_t packet = 1; packet <= 2000; ++packet) {
      cp.UpdateWritePointer(packet);
      Until([&] { return cp.completed.load() == packet; });
    }
    std::atomic<unsigned> callbacks{0};
    for (unsigned i = 1; i <= 200; ++i) {
      cp.CallInThread([&] { ++callbacks; });
      Until([&] { return callbacks.load() == i; });
    }
    // A stale auto-reset signal and pointer publication must not lose work.
    cp.write_ptr_index_event_->Set();
    cp.UpdateWritePointer(2001);
    Until([&] { return cp.completed.load() == 2001; });
    cp.worker_running_ = false;
    cp.write_ptr_index_event_->Set();
    worker.join();
    assert(cp.completed == 2001 && callbacks == 200);
  }
  std::puts("PASS: extracted worker at spin limits 0/16/500; 6003 pointer updates, 600 callbacks, wake-before-wait and shutdown");
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'command-ring-wait-test.cpp'
path.write_text(code)
binary = args.work_dir / 'command-ring-wait-test'
subprocess.run([args.cxx, '-std=c++23', '-O2', '-pthread', '-fsanitize=address,undefined',
                str(path), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True, timeout=45)
