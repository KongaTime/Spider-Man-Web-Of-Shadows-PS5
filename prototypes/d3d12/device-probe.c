/* SPDX-License-Identifier: MIT
 * Standalone D3D12 device and transfer test. No guest game code or assets.
 */
#define VK_NO_PROTOTYPES
#include <vkd3d.h>
#include <vkd3d_device_vkd3d_ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char *);
int sceKernelDebugOutText(int, const char *);
static FILE *output;
#define LOG(...) do { char line[1024]; snprintf(line, sizeof(line), __VA_ARGS__); \
    sceKernelDebugOutText(0, line); if (output) { fputs(line, output); fflush(output); } } while (0)
#define HR(call) do { HRESULT result = (call); LOG(#call "=%08x\n", (unsigned)result); \
    if (FAILED(result)) { LOG("FAIL stopped at line %d\n", __LINE__); return 1; } } while (0)

#include "rov-test.inc"
#include "present-test.inc"

static int transfer(ID3D12Device *device)
{
    ID3D12CommandQueue *queue;
    D3D12_COMMAND_QUEUE_DESC queue_desc = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
    HR(ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue));
    ID3D12CommandAllocator *allocator;
    HR(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&allocator));
    ID3D12GraphicsCommandList *list;
    HR(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
        NULL, &IID_ID3D12GraphicsCommandList, (void **)&list));
    ID3D12Resource *source, *destination;
    D3D12_RESOURCE_DESC resource = { .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = 4096, .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1,
        .SampleDesc = {1, 0}, .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR };
    D3D12_HEAP_PROPERTIES heap = { .Type = D3D12_HEAP_TYPE_UPLOAD, .CreationNodeMask = 1, .VisibleNodeMask = 1 };
    HR(ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &resource,
        D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&source));
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    HR(ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &resource,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&destination));
    uint32_t *mapped;
    D3D12_RANGE empty = {0, 0}, full = {0, 4096};
    HR(ID3D12Resource_Map(source, 0, &empty, (void **)&mapped));
    for (unsigned i = 0; i < 1024; ++i) mapped[i] = 0x5a17c300u ^ (i * 2654435761u);
    ID3D12Resource_Unmap(source, 0, &full);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, destination, 0, source, 0, 4096);
    HR(ID3D12GraphicsCommandList_Close(list));
    ID3D12CommandList *submitted = (ID3D12CommandList *)list;
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &submitted);
    ID3D12Fence *fence;
    HR(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
    HR(ID3D12CommandQueue_Signal(queue, fence, 1));
    unsigned attempt;
    for (attempt = 0; attempt < 10000; ++attempt) {
        uint64_t completed = ID3D12Fence_GetCompletedValue(fence);
        if (completed == UINT64_MAX) { LOG("FAIL device removed\n"); return 1; }
        if (completed >= 1) break;
        usleep(1000);
    }
    if (attempt == 10000) { LOG("FAIL GPU fence timeout\n"); return 1; }
    HR(ID3D12Resource_Map(destination, 0, &full, (void **)&mapped));
    unsigned errors = 0;
    for (unsigned i = 0; i < 1024; ++i) if (mapped[i] != (0x5a17c300u ^ (i * 2654435761u))) ++errors;
    ID3D12Resource_Unmap(destination, 0, &empty);
    LOG("D3D12_GPU_COPY checked_words=1024 mismatches=%u\n", errors);
    ID3D12Fence_Release(fence); ID3D12Resource_Release(destination); ID3D12Resource_Release(source);
    ID3D12GraphicsCommandList_Release(list); ID3D12CommandAllocator_Release(allocator); ID3D12CommandQueue_Release(queue);
    return errors != 0;
}

int main(void)
{
    output = fopen("/app0/d3d12-device.txt", "w");
    setenv("VKD3D_DEBUG", "info", 1);
    setenv("VKD3D_LOG_FILE", "/app0/vkd3d-device.log", 1);
    setenv("VKD3D_SHADER_CACHE_PATH", "0", 1);
    LOG("D3D12 native device probe 004; vkd3d-proton static + PS5 RADV\n");
    const char *instance_extensions[] = {"VK_KHR_surface", "VK_KHR_display"};
    const char *device_extensions[] = {"VK_KHR_swapchain"};
    struct vkd3d_instance_create_info instance = { .pfn_vkGetInstanceProcAddr = vk_icdGetInstanceProcAddr,
        .instance_extensions = instance_extensions, .instance_extension_count = 2 };
    struct vkd3d_device_create_info info = { .minimum_feature_level = D3D_FEATURE_LEVEL_11_0,
        .instance_create_info = &instance, .device_extensions = device_extensions, .device_extension_count = 1 };
    ID3D12Device *device;
    HR(vkd3d_create_device(&info, &IID_ID3D12Device, (void **)&device));
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    HR(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)));
    LOG("D3D12 options ROVsSupported=%u ResourceBindingTier=%u\n", options.ROVsSupported, options.ResourceBindingTier);
    int result = transfer(device);
    if (!result && options.ROVsSupported) result = rov_draw(device);
    else if (!options.ROVsSupported) { LOG("FAIL D3D12 ROVs unsupported\n"); result = 1; }
    if (!result) result = present_test(device);
    ID3D12Device_Release(device);
    LOG("DEVICE_PROBE_COMPLETE result=%d; isolated prototype, game integration and performance untested\n", result);
    if (output) fclose(output);
    return result;
}
