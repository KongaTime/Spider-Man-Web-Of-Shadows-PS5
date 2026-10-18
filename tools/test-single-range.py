"""Host regression harness for the single-range optimization; requires Linux/WSL and Clang."""
from pathlib import Path
import subprocess

import argparse
parser = argparse.ArgumentParser(description="Compare patched SDK range requests with HEAD under host sanitizers")
parser.add_argument('sdk', type=Path)
parser.add_argument('--work-dir', type=Path, required=True)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
sdk = args.sdk.resolve()
test_dir = args.work_dir.resolve()
test_dir.mkdir(parents=True, exist_ok=True)
path = sdk / 'src/graphics/shared_memory.cpp'
current = path.read_text()
original = subprocess.check_output(['git', '-C', str(sdk), 'show', 'HEAD:src/graphics/shared_memory.cpp']).decode()
def methods(source, name):
    begin = source.index('bool SharedMemory::RequestRanges(')
    end = source.index('std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallbackThunk', begin)
    return source[begin:end].replace('SharedMemory::', name + '::')

code = r'''
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <random>
#include <span>
#include <utility>
#include <vector>
static size_t allocations = 0;
void* operator new(size_t size) { ++allocations; if (void* p = std::malloc(size)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#define SCOPE_profile_cpu_f(...) ((void)0)
#define COUNT_profile_set(...) ((void)0)
namespace rex {
bool bit_scan_forward(uint64_t value, uint32_t* bit) {
  if (!value) return false;
  *bit = std::countr_zero(value);
  return true;
}
}
using Range = std::pair<uint32_t, uint32_t>;
struct Region {
  std::mutex mutex;
  auto Acquire() { return std::unique_lock<std::mutex>(mutex); }
};
struct State {
  static constexpr uint32_t kBufferSize = 1u << 29;
  uint32_t page_size_log2_ = 12;
  std::vector<uint64_t> system_page_flags_valid_ = std::vector<uint64_t>(2048);
  std::vector<Range> upload_ranges_, allocation_calls, uploaded;
  Region global_critical_region_;
  int fail_allocation_call = -1;
  bool upload_success = true;
  bool EnsureHostGpuMemoryAllocated(uint32_t start, uint32_t length) {
    allocation_calls.emplace_back(start, length);
    return int(allocation_calls.size()) != fail_allocation_call;
  }
  bool UploadRanges(const std::vector<Range>& ranges) { uploaded = ranges; return upload_success; }
};
struct Original : State { bool RequestRanges(const Range*, size_t); bool RequestRange(uint32_t, uint32_t); };
struct Optimized : State { bool RequestRanges(const Range*, size_t); bool RequestRange(uint32_t, uint32_t); };
'''
code += methods(original, 'Original') + methods(current, 'Optimized')
code += r'''
int main() {
  std::mt19937_64 random(0x57534f53);
  Original old;
  Optimized fast;
  for (int test = 0; test < 12000; ++test) {
    std::vector<Range> ranges;
    const size_t count = test < 6000 ? 1 : random() % 9;
    for (size_t i = 0; i < count; ++i) {
      uint32_t start = random() % (State::kBufferSize + 8192u);
      uint32_t length = random() % 1000000u;
      if (test % 7 == 0) length = 0;
      if (test % 11 == 0) start = State::kBufferSize - uint32_t(random() % 100u);
      if (test % 13 == 0) { start = 0; length = State::kBufferSize; }
      if (test % 17 == 0 && i) start = ranges.back().first;
      if (test % 19 == 0) { start = UINT32_MAX; length = random() % 2; }
      ranges.emplace_back(start, length);
    }
    for (auto& word : old.system_page_flags_valid_) {
      word = test % 3 == 0 ? UINT64_MAX : (test % 3 == 1 ? 0 : random());
    }
    fast.system_page_flags_valid_ = old.system_page_flags_valid_;
    old.allocation_calls.clear(); fast.allocation_calls.clear();
    old.uploaded.clear(); fast.uploaded.clear();
    old.upload_ranges_ = fast.upload_ranges_ = {{9, 3}};
    old.fail_allocation_call = fast.fail_allocation_call = test % 5 == 0 ? 1 : -1;
    old.upload_success = fast.upload_success = test % 23 != 0;
    const bool null_ranges = test % 29 == 0;
    const Range* data = null_ranges ? nullptr : ranges.data();
    assert(old.RequestRanges(data, count) == fast.RequestRanges(data, count));
    assert(old.allocation_calls == fast.allocation_calls);
    assert(old.uploaded == fast.uploaded);
    assert(old.upload_ranges_ == fast.upload_ranges_);
    assert(old.system_page_flags_valid_ == fast.system_page_flags_valid_);
  }
  old.system_page_flags_valid_.assign(2048, UINT64_MAX);
  fast.system_page_flags_valid_ = old.system_page_flags_valid_;
  old.allocation_calls.clear(); fast.allocation_calls.clear();
  old.allocation_calls.reserve(10001); fast.allocation_calls.reserve(10001);
  old.fail_allocation_call = fast.fail_allocation_call = -1;
  const auto before_old = allocations;
  for (int i = 0; i < 10000; ++i) assert(old.RequestRange(0x1000, 0x3000));
  const auto old_count = allocations - before_old;
  const auto before_fast = allocations;
  for (int i = 0; i < 10000; ++i) assert(fast.RequestRange(0x1000, 0x3000));
  const auto fast_count = allocations - before_fast;
  assert(old_count == 10000 && fast_count == 0);
  std::printf("PASS: 12000 differential cases; 10000 resident requests allocated %zu times before, %zu after\n", old_count, fast_count);
}
'''
(test_dir / 'range-optimization-test.cpp').write_text(code, newline='\n')
subprocess.run([args.cxx, '-std=c++23', '-O2', '-fsanitize=address,undefined',
                str(test_dir / 'range-optimization-test.cpp'),
                '-o', str(test_dir / 'range-optimization-test')], check=True)
subprocess.run([str(test_dir / 'range-optimization-test')], check=True)
