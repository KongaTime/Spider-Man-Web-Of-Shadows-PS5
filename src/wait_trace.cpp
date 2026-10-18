#include "wait_trace.h"
#include <array>
#include <atomic>
#include <cstring>
#include <rex/chrono/clock.h>
#include <rex/logging.h>

namespace wos::trace {
namespace {
// Bounded observers: no per-wait allocation and no new shared mutex. Only
// sampled new rows resolve a handle. Atomic registry entries are self-contained.
static_assert(std::atomic<uint64_t>::is_always_lock_free);
std::array<std::atomic<uint64_t>, 32> watched{};
struct WaitRow {
  WaitToken key;
  ObjectInfo info;
  uint64_t count = 0, ns = 0, success = 0, timeouts = 0, other = 0;
};
struct WaitWindow {
  bool started = false;
  uint32_t sequence = 0;
  uint64_t dropped = 0;
  std::array<WaitRow, 16> rows{};
};
thread_local WaitWindow waits;
bool Same(const WaitToken& a, const WaitToken& b) {
  return a.kind == b.kind && a.object == b.object && a.caller == b.caller &&
      a.timeout == b.timeout && a.missing_timeout == b.missing_timeout && a.alertable == b.alertable;
}
bool Watch(uint32_t handle, uint32_t guest) {
  const uint64_t key = (uint64_t(handle) << 32) | guest;
  if (!key) return true;
  for (auto& slot : watched) {
    uint64_t old = slot.load(std::memory_order_relaxed);
    if (old == key) return true;
    if (!old && slot.compare_exchange_strong(old, key, std::memory_order_relaxed)) return true;
    if (old == key) return true;
  }
  return false;
}
struct SignalRow { uint32_t kind = 0, object = 0, caller = 0, result = 0; uint64_t count = 0; };
struct SignalWindow { uint64_t last = 0, dropped = 0; std::array<SignalRow, 16> rows{}; };
thread_local SignalWindow signals;
}  // namespace

WaitToken BeginWait(size_t kind, uint32_t object, uint32_t caller,
                    uint32_t timeout_ptr, uint32_t alertable, const uint8_t* base) {
  if (!waits.started || (kind != 2 && kind != 4)) return {};
  // NtSingle is high-frequency polling: observe 1/64 calls, not every handle lookup.
  if (kind == 2 && (++waits.sequence & 63)) return {};
  WaitToken token{true, timeout_ptr == 0, uint32_t(kind), object, caller, alertable, 0};
  if (timeout_ptr) {
    uint64_t value;
    std::memcpy(&value, base + timeout_ptr, sizeof(value));
    token.timeout = int64_t(__builtin_bswap64(value));
  }
  return token;
}
void EndWait(const WaitToken& token, uint64_t ns, uint32_t result) {
  if (!token.active) return;
  for (auto& row : waits.rows) {
    if (row.count && !Same(row.key, token)) continue;
    if (!row.count) {
      row.key = token;
      if (token.kind == 2) {
        row.info = ResolveObject(token.object);
        if (!Watch(token.object, row.info.guest)) ++waits.dropped;
      }
    }
    ++row.count; row.ns += ns;
    if (result == 0) ++row.success;
    else if (result == 0x102) ++row.timeouts;
    else ++row.other;
    return;
  }
  ++waits.dropped;
}
void ResetWaitWindow() {
  // Keep sampling cadence across report windows; discard startup observations.
  auto sequence = waits.sequence;
  waits = {}; waits.started = true; waits.sequence = sequence;
}
void ReportWaitWindow(uint64_t frames) {
  const auto thread = CurrentThread();
  for (const auto& row : waits.rows) {
    if (!row.count) continue;
    REXLOG_INFO("WOS wait detail: kind {}, thread {}, entry {:#x}, priority {}, object {:#x}, "
                "type {}, guest {:#x}, caller {:#x}, timeout100ns {}, missing {}, alertable {}, "
                "samples {}, success {}, timedout {}, other {}, mean-us {:.3f}, frames {}",
                row.key.kind, thread.id, thread.entry, thread.priority, row.key.object,
                row.info.type, row.info.guest, row.key.caller, row.key.timeout,
                row.key.missing_timeout, row.key.alertable, row.count, row.success,
                row.timeouts, row.other, double(row.ns) / (1000.0 * row.count), frames);
  }
  REXLOG_INFO("WOS wait detail coverage: NtSingle 1/64, delay all; dropped-or-unwatched {}", waits.dropped);
  ResetWaitWindow();
}
void Signal(uint32_t kind, uint32_t object, uint32_t caller, uint32_t result) {
  bool matched = false;
  for (const auto& slot : watched) {
    const auto key = slot.load(std::memory_order_relaxed);
    if (key && object && (kind == 1 ? uint32_t(key >> 32) == object : uint32_t(key) == object)) {
      matched = true; break;
    }
  }
  if (!matched) return;
  bool recorded = false;
  for (auto& row : signals.rows) {
    if (row.count && (row.kind != kind || row.object != object || row.caller != caller)) continue;
    row.kind = kind; row.object = object; row.caller = caller; row.result = result; ++row.count;
    recorded = true; break;
  }
  if (!recorded) ++signals.dropped;
  const auto now = rex::chrono::Clock::QueryHostTickCount();
  if (!signals.last) signals.last = now;
  if (now - signals.last < 2000000000ull) return;
  const auto thread = CurrentThread();
  for (const auto& row : signals.rows) {
    if (!row.count) continue;
    REXLOG_INFO("WOS signal detail: kind {}, thread {}, entry {:#x}, priority {}, object {:#x}, "
                "caller {:#x}, calls {}, last-return {:#x}",
                row.kind, thread.id, thread.entry, thread.priority, row.object, row.caller,
                row.count, row.result);
  }
  REXLOG_INFO("WOS signal detail coverage: KeSetEvent 0, NtSetEvent 1, KeReleaseSemaphore 2; dropped {}", signals.dropped);
  signals = {}; signals.last = now;
}
}  // namespace wos::trace
