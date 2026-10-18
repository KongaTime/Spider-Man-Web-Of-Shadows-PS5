/* SPDX-License-Identifier: LGPL-2.1-or-later
 * PS5 in-process events for the native vkd3d experiment. No Linux eventfd ABI.
 * HANDLE values passed to this port must refer to these objects. They are not
 * interchangeable with Win32 handles, kernel descriptors, or other SDK events.
 */
#include <errno.h>
#include <pthread.h>
#include <time.h>

struct vkd3d_ps5_event
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    uint64_t value;
};
typedef struct {
    struct vkd3d_ps5_event *event;
    enum vkd3d_native_sync_handle_type type;
} vkd3d_native_sync_handle;

static inline bool vkd3d_native_sync_handle_eq(vkd3d_native_sync_handle a, vkd3d_native_sync_handle b)
{ return a.event == b.event; }

static inline vkd3d_native_sync_handle vkd3d_native_sync_handle_wrap(HANDLE value,
        enum vkd3d_native_sync_handle_type type)
{
    vkd3d_native_sync_handle result = { value, value ? type : VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE };
    return result;
}

static inline int vkd3d_native_sync_handle_release(vkd3d_native_sync_handle handle, UINT count)
{
    if (!handle.event || handle.type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE) return -1;
    struct vkd3d_ps5_event *event = handle.event;
    pthread_mutex_lock(&event->mutex);
    uint64_t previous = event->value;
    if (handle.type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT) event->value = 1;
    else if (count > DXGI_MAX_SWAP_CHAIN_BUFFERS - event->value) {
        pthread_mutex_unlock(&event->mutex); return -1;
    }
    else event->value += count;
    pthread_cond_broadcast(&event->condition);
    pthread_mutex_unlock(&event->mutex);
    return (int)previous;
}
static inline HRESULT vkd3d_native_sync_handle_signal(vkd3d_native_sync_handle handle)
{ return vkd3d_native_sync_handle_release(handle, 1) >= 0 ? S_OK : E_FAIL; }

static inline bool vkd3d_ps5_wait(vkd3d_native_sync_handle handle, const struct timespec *deadline)
{
    if (!handle.event || handle.type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE) return false;
    struct vkd3d_ps5_event *event = handle.event;
    pthread_mutex_lock(&event->mutex);
    int rc = 0;
    while (!event->value && !rc) rc = deadline ?
        pthread_cond_timedwait(&event->condition, &event->mutex, deadline) :
        pthread_cond_wait(&event->condition, &event->mutex);
    /* A signal concurrent with the timeout still wins when observed locked. */
    bool acquired = event->value != 0;
    if (acquired) --event->value;
    pthread_mutex_unlock(&event->mutex);
    return acquired;
}
static inline bool vkd3d_native_sync_handle_acquire(vkd3d_native_sync_handle handle)
{ return vkd3d_ps5_wait(handle, NULL); }
static inline bool vkd3d_native_sync_handle_acquire_timeout(vkd3d_native_sync_handle handle, unsigned int ms)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline)) return false;
    deadline.tv_sec += ms / 1000;
    deadline.tv_nsec += (ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L; }
    return vkd3d_ps5_wait(handle, &deadline);
}
static inline bool vkd3d_native_sync_handle_is_valid(vkd3d_native_sync_handle handle)
{ return handle.event && handle.type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE; }

static inline HRESULT vkd3d_native_sync_handle_create(UINT initial,
        enum vkd3d_native_sync_handle_type type, vkd3d_native_sync_handle *handle)
{
    *handle = (vkd3d_native_sync_handle){0};
    if (type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT && type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE)
        return E_INVALIDARG;
    if (type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE && initial > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        return E_INVALIDARG;
    struct vkd3d_ps5_event *event = vkd3d_calloc(1, sizeof(*event));
    if (!event) return E_OUTOFMEMORY;
    if (pthread_mutex_init(&event->mutex, NULL)) { vkd3d_free(event); return E_FAIL; }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr)) { pthread_mutex_destroy(&event->mutex); vkd3d_free(event); return E_FAIL; }
    int rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!rc) rc = pthread_cond_init(&event->condition, &attr);
    pthread_condattr_destroy(&attr);
    if (rc) { pthread_mutex_destroy(&event->mutex); vkd3d_free(event); return E_FAIL; }
    event->value = type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT ? !!initial : initial;
    *handle = (vkd3d_native_sync_handle){event, type};
    return S_OK;
}
static inline void vkd3d_native_sync_handle_destroy(vkd3d_native_sync_handle handle)
{
    if (!vkd3d_native_sync_handle_is_valid(handle)) return;
    pthread_cond_destroy(&handle.event->condition);
    pthread_mutex_destroy(&handle.event->mutex);
    vkd3d_free(handle.event);
}
