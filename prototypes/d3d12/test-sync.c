/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef void *HANDLE;
typedef unsigned UINT;
typedef int HRESULT;
#define S_OK 0
#define E_FAIL (-1)
#define E_INVALIDARG (-2)
#define E_OUTOFMEMORY (-3)
#define DXGI_MAX_SWAP_CHAIN_BUFFERS 16
#define vkd3d_calloc calloc
#define vkd3d_free free
enum vkd3d_native_sync_handle_type {
    VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE,
    VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT,
    VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE,
};
#include "vkd3d_ps5_sync.h"

static void *signal_after_delay(void *argument)
{
    struct timespec delay = {0, 2000000}; nanosleep(&delay, NULL);
    assert(vkd3d_native_sync_handle_signal(*(vkd3d_native_sync_handle *)argument) == S_OK);
    return NULL;
}
int main(void)
{
    vkd3d_native_sync_handle event, semaphore;
    assert(vkd3d_native_sync_handle_create(0, VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT, &event) == S_OK);
    assert(!vkd3d_native_sync_handle_acquire_timeout(event, 0));
    assert(vkd3d_native_sync_handle_signal(event) == S_OK);
    assert(vkd3d_native_sync_handle_signal(event) == S_OK);
    assert(vkd3d_native_sync_handle_acquire_timeout(event, 0));
    assert(!vkd3d_native_sync_handle_acquire_timeout(event, 1));
    for (int i = 0; i < 100; ++i) {
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, signal_after_delay, &event));
        assert(vkd3d_native_sync_handle_acquire_timeout(event, 2000));
        assert(!pthread_join(thread, NULL));
        assert(!vkd3d_native_sync_handle_acquire_timeout(event, 0));
    }
    assert(vkd3d_native_sync_handle_create(2, VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE, &semaphore) == S_OK);
    assert(vkd3d_native_sync_handle_acquire_timeout(semaphore, 0));
    assert(vkd3d_native_sync_handle_acquire_timeout(semaphore, 0));
    assert(!vkd3d_native_sync_handle_acquire_timeout(semaphore, 0));
    assert(vkd3d_native_sync_handle_release(semaphore, DXGI_MAX_SWAP_CHAIN_BUFFERS) == 0);
    assert(vkd3d_native_sync_handle_signal(semaphore) == E_FAIL);
    for (int i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; ++i) assert(vkd3d_native_sync_handle_acquire_timeout(semaphore, 0));
    assert(!vkd3d_native_sync_handle_acquire_timeout(semaphore, 0));
    vkd3d_native_sync_handle_destroy(semaphore);
    vkd3d_native_sync_handle_destroy(event);
    puts("PASS native sync: auto-reset, counted semaphore, overflow, timeout, 100 cross-thread signals");
}
