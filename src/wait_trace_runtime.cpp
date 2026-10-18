#include "wait_trace.h"
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

namespace wos::trace {
ObjectInfo ResolveObject(uint32_t handle) {
  auto* state = REX_KERNEL_STATE();
  if (!state) return {};
  auto object = state->object_table()->LookupObject<rex::system::XObject>(handle);
  return object ? ObjectInfo{object->guest_object(), uint32_t(object->type())} : ObjectInfo{};
}
ThreadInfo CurrentThread() {
  auto* thread = rex::system::XThread::GetCurrentThread();
  return thread ? ThreadInfo{thread->thread_id(), thread->creation_params()->start_address,
                             thread->priority()} : ThreadInfo{};
}
}  // namespace wos::trace
