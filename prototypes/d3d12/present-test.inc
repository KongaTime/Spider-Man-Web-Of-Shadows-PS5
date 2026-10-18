/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native VK_KHR_display presentation follows PS5_Vulkan's radv_smoke.c
 * (Copyright 2026 Mihawk-99). The image itself is written through D3D12.
 * This deliberately serial proof is not a frame-rate benchmark.
 */
#define VK_TRY(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    LOG("FAIL " #call "=%d line=%d\n", r, __LINE__); return 1; } } while (0)
#define HR_TRY(call) do { HRESULT r = (call); if (FAILED(r)) { \
    LOG("FAIL " #call "=%08x line=%d\n", (unsigned)r, __LINE__); return 1; } } while (0)

static int present_test(ID3D12Device *device)
{
    VkDevice vd = vkd3d_get_vk_device(device);
    VkPhysicalDevice physical = vkd3d_get_vk_physical_device(device);
    VkInstance vi = vkd3d_instance_get_vk_instance(vkd3d_instance_from_device(device));
    #define INSTANCE_PROC(n) PFN_vk##n n = (PFN_vk##n)vk_icdGetInstanceProcAddr(vi, "vk" #n); \
        if (!n) { LOG("FAIL missing vk" #n "\n"); return 1; }
    INSTANCE_PROC(GetDeviceProcAddr); INSTANCE_PROC(GetPhysicalDeviceDisplayPropertiesKHR);
    INSTANCE_PROC(GetDisplayModePropertiesKHR); INSTANCE_PROC(CreateDisplayPlaneSurfaceKHR);
    INSTANCE_PROC(GetPhysicalDeviceSurfaceCapabilitiesKHR); INSTANCE_PROC(GetPhysicalDeviceSurfaceSupportKHR);
    INSTANCE_PROC(DestroySurfaceKHR);
    #define DEVICE_PROC(n) PFN_vk##n n = (PFN_vk##n)GetDeviceProcAddr(vd, "vk" #n); \
        if (!n) { LOG("FAIL missing vk" #n "\n"); return 1; }
    DEVICE_PROC(CreateSwapchainKHR); DEVICE_PROC(DestroySwapchainKHR); DEVICE_PROC(GetSwapchainImagesKHR);
    DEVICE_PROC(CreateSemaphore); DEVICE_PROC(DestroySemaphore); DEVICE_PROC(AcquireNextImageKHR);
    DEVICE_PROC(QueueSubmit); DEVICE_PROC(QueueWaitIdle); DEVICE_PROC(QueuePresentKHR);
    DEVICE_PROC(CreateCommandPool); DEVICE_PROC(DestroyCommandPool); DEVICE_PROC(AllocateCommandBuffers);
    DEVICE_PROC(ResetCommandPool); DEVICE_PROC(BeginCommandBuffer); DEVICE_PROC(EndCommandBuffer);
    DEVICE_PROC(CmdPipelineBarrier); DEVICE_PROC(CmdBlitImage);
    DEVICE_PROC(CmdCopyImageToBuffer);
    DEVICE_PROC(CreateFence); DEVICE_PROC(DestroyFence); DEVICE_PROC(WaitForFences); DEVICE_PROC(ResetFences);
    VkDisplayPropertiesKHR display;
    uint32_t n = 1;
    VK_TRY(GetPhysicalDeviceDisplayPropertiesKHR(physical, &n, &display));
    if (n != 1) { LOG("FAIL expected one native display\n"); return 1; }
    VkDisplayModePropertiesKHR modes[8]; n = 8;
    VK_TRY(GetDisplayModePropertiesKHR(physical, display.display, &n, modes));
    if (!n) { LOG("FAIL no display modes\n"); return 1; }
    VkExtent2D extent = modes[0].parameters.visibleRegion;
    LOG("D3D12_PRESENT display=%ux%u refresh_millihz=%u\n", extent.width, extent.height, modes[0].parameters.refreshRate);
    VkDisplaySurfaceCreateInfoKHR si = { .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
        .displayMode = modes[0].displayMode, .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR, .imageExtent = extent };
    VkSurfaceKHR surface;
    VK_TRY(CreateDisplayPlaneSurfaceKHR(vi, &si, NULL, &surface));
    ID3D12CommandQueue *queue;
    D3D12_COMMAND_QUEUE_DESC qd = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
    HR(ID3D12Device_CreateCommandQueue(device, &qd, &IID_ID3D12CommandQueue, (void **)&queue));
    uint32_t family = vkd3d_get_vk_queue_family_index(queue);
    VkBool32 support = VK_FALSE;
    VK_TRY(GetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &support));
    VkSurfaceCapabilitiesKHR caps;
    VK_TRY(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    const VkImageUsageFlags required_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!support || (caps.supportedUsageFlags & required_usage) != required_usage) {
        LOG("FAIL native surface transfer unsupported\n"); return 1;
    }
    VkSwapchainCreateInfoKHR sci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface, .minImageCount = 3, .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = required_usage, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE };
    VkSwapchainKHR swapchain;
    VK_TRY(CreateSwapchainKHR(vd, &sci, NULL, &swapchain));
    VkImage images[8]; n = 8;
    VK_TRY(GetSwapchainImagesKHR(vd, swapchain, &n, images));
    VkSemaphore acquired, rendered;
    VkSemaphoreCreateInfo sem = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VK_TRY(CreateSemaphore(vd, &sem, NULL, &acquired)); VK_TRY(CreateSemaphore(vd, &sem, NULL, &rendered));
    VkCommandPool pool;
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family };
    VK_TRY(CreateCommandPool(vd, &pci, NULL, &pool));
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VK_TRY(AllocateCommandBuffers(vd, &cai, &command));
    VkFence fence;
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_TRY(CreateFence(vd, &fci, NULL, &fence));
    ID3D12CommandAllocator *allocator; ID3D12GraphicsCommandList *list;
    HR(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator));
    HR(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
        &IID_ID3D12GraphicsCommandList, (void **)&list));
    ID3D12Resource *target;
    D3D12_RESOURCE_DESC rd = { .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = 1280, .Height = 720,
        .DepthOrArraySize = 1, .MipLevels = 1, .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .SampleDesc = {1, 0},
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
    D3D12_HEAP_PROPERTIES hp = { .Type = D3D12_HEAP_TYPE_DEFAULT, .CreationNodeMask = 1, .VisibleNodeMask = 1 };
    HR(ID3D12Device_CreateCommittedResource(device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&target));
    ID3D12DescriptorHeap *heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = 1 };
    HR(ID3D12Device_CreateDescriptorHeap(device, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    ID3D12Device_CreateRenderTargetView(device, target, NULL, rtv);
    ID3D12DXVKInteropDevice *interop;
    HR(ID3D12Device_QueryInterface(device, &IID_ID3D12DXVKInteropDevice, (void **)&interop));
    UINT64 image_handle, offset;
    HR(ID3D12DXVKInteropDevice_GetVulkanResourceInfo(interop, target, &image_handle, &offset));
    VkImage source = (VkImage)image_handle; VkImageLayout source_layout;
    ID3D12DXVKInteropDevice_GetVulkanImageLayout(interop, target, D3D12_RESOURCE_STATE_COPY_SOURCE, &source_layout);
    ID3D12Resource *pixel_readback;
    D3D12_RESOURCE_DESC buffer_desc = { .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = 4096,
        .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {1, 0}, .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR };
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    HR(ID3D12Device_CreateCommittedResource(device, &hp, D3D12_HEAP_FLAG_NONE, &buffer_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&pixel_readback));
    UINT64 readback_handle, readback_offset;
    HR(ID3D12DXVKInteropDevice_GetVulkanResourceInfo(interop, pixel_readback, &readback_handle, &readback_offset));
    unsigned frames = 0;
    for (; frames < 240; ++frames) {
        if (frames) {
            HR_TRY(ID3D12CommandAllocator_Reset(allocator));
            HR_TRY(ID3D12GraphicsCommandList_Reset(list, allocator, NULL));
            D3D12_RESOURCE_BARRIER b = { .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET} };
            ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
        }
        float color[4] = {.05f, .2f + .4f * (frames % 60) / 60.0f, .6f, 1};
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, color, 0, NULL);
        D3D12_RESOURCE_BARRIER b = { .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COPY_SOURCE} };
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
        HR_TRY(ID3D12GraphicsCommandList_Close(list));
        ID3D12CommandList *submitted = (ID3D12CommandList *)list;
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &submitted);
        uint32_t index;
        VK_TRY(AcquireNextImageKHR(vd, swapchain, 10000000000ull, acquired, VK_NULL_HANDLE, &index));
        VK_TRY(ResetCommandPool(vd, pool, 0));
        VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VK_TRY(BeginCommandBuffer(command, &begin));
        VkImageMemoryBarrier barriers[2] = {
            { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .oldLayout = source_layout, .newLayout = source_layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = source, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} },
            { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[index], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} }
        };
        CmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 2, barriers);
        VkImageBlit blit = { .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffsets = {{0, 0, 0}, {1280, 720, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstOffsets = {{0, 0, 0}, {(int32_t)extent.width, (int32_t)extent.height, 1}} };
        CmdBlitImage(command, source, source_layout, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barriers[1].dstAccessMask = 0;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        if (frames == 239) {
            barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &barriers[1]);
            VkBufferImageCopy pixel = { .bufferOffset = readback_offset,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {1, 1, 1} };
            CmdCopyImageToBuffer(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                (VkBuffer)readback_handle, 1, &pixel);
            VkMemoryBarrier host = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 1, &host, 0, NULL, 0, NULL);
            barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barriers[1].dstAccessMask = 0; barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        barriers[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, NULL, 0, NULL, 1, &barriers[1]);
        VK_TRY(EndCommandBuffer(command));
        VkQueue vq = vkd3d_acquire_vk_queue(queue);
        if (!vq) { LOG("FAIL acquire interop queue\n"); return 1; }
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquired, .pWaitDstStageMask = &stage, .commandBufferCount = 1,
            .pCommandBuffers = &command, .signalSemaphoreCount = 1, .pSignalSemaphores = &rendered };
        VkResult result = QueueSubmit(vq, 1, &submit, fence);
        if (result == VK_SUCCESS) {
            VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
                .pWaitSemaphores = &rendered, .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &index };
            result = QueuePresentKHR(vq, &present);
        }
        if (result == VK_SUCCESS) result = WaitForFences(vd, 1, &fence, VK_TRUE, 10000000000ull);
        if (result == VK_SUCCESS) result = QueueWaitIdle(vq);
        vkd3d_release_vk_queue(queue);
        if (result != VK_SUCCESS) { LOG("FAIL native present frame=%u result=%d\n", frames, result); return 1; }
        VK_TRY(ResetFences(vd, 1, &fence));
    }
    unsigned char *pixel;
    D3D12_RANGE pixel_range = {0, 4}, empty_range = {0, 0};
    HR(ID3D12Resource_Map(pixel_readback, 0, &pixel_range, (void **)&pixel));
    LOG("D3D12_PRESENT_PIXEL bgra=%u,%u,%u,%u expected=153,151,13,255\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    int pixel_error = pixel[0] != 153 || pixel[1] != 151 || pixel[2] != 13 || pixel[3] != 255;
    ID3D12Resource_Unmap(pixel_readback, 0, &empty_range); ID3D12Resource_Release(pixel_readback);
    LOG("D3D12_PRESENT_COMPLETE frames=%u pixel_mismatches=%d; visual confirmation separate; not a gameplay benchmark\n", frames, pixel_error);
    ID3D12DXVKInteropDevice_Release(interop); ID3D12DescriptorHeap_Release(heap); ID3D12Resource_Release(target);
    ID3D12GraphicsCommandList_Release(list); ID3D12CommandAllocator_Release(allocator); ID3D12CommandQueue_Release(queue);
    DestroyFence(vd, fence, NULL); DestroyCommandPool(vd, pool, NULL);
    DestroySemaphore(vd, acquired, NULL); DestroySemaphore(vd, rendered, NULL);
    DestroySwapchainKHR(vd, swapchain, NULL); DestroySurfaceKHR(vi, surface, NULL);
    return pixel_error;
}
