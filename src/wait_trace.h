#pragma once
#include <cstddef>
#include <cstdint>

namespace wos::trace {
struct ObjectInfo { uint32_t guest = 0, type = 0; };
struct ThreadInfo { uint32_t id = 0, entry = 0; int32_t priority = 0; };
ObjectInfo ResolveObject(uint32_t handle);
ThreadInfo CurrentThread();
struct WaitToken {
  bool active = false, missing_timeout = false;
  uint32_t kind = 0, object = 0, caller = 0, alertable = 0;
  int64_t timeout = 0;
};
WaitToken BeginWait(size_t kind, uint32_t object, uint32_t caller,
                    uint32_t timeout_ptr, uint32_t alertable, const uint8_t* base);
void EndWait(const WaitToken&, uint64_t elapsed_ns, uint32_t result);
void ResetWaitWindow();
void ReportWaitWindow(uint64_t frames);
void Signal(uint32_t kind, uint32_t object, uint32_t caller, uint32_t result);
}  // namespace wos::trace
