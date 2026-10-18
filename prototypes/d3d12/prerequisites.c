/* SPDX-License-Identifier: MIT
 * Native PS5 prerequisite inventory for a separate D3D12 experiment.
 * This is a Vulkan feature probe, not a D3D12 implementation or benchmark.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char *);
int sceKernelDebugOutText(int, const char *);
static FILE *output;
#define LOG(...) do { char line[1024]; snprintf(line, sizeof(line), __VA_ARGS__); \
    sceKernelDebugOutText(0, line); if (output) { fputs(line, output); fflush(output); } } while (0)
#define FEATURE(s, f) LOG(#s "." #f "=%u\n", (unsigned)s.f)

int main(void)
{
    output = fopen("/app0/d3d12-prerequisites.txt", "w");
    LOG("D3D12 prerequisite probe 001; no game changes; no D3D12 execution yet\n");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (!create) { LOG("FAIL missing vkCreateInstance\n"); return 1; }
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "WoS D3D12 prerequisites", .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult rc = create(&ci, NULL, &instance);
    LOG("vkCreateInstance=%d\n", rc);
    if (rc != VK_SUCCESS) return 1;
    #define LOAD(n) PFN_vk##n n = (PFN_vk##n)vk_icdGetInstanceProcAddr(instance, "vk" #n)
    LOAD(EnumeratePhysicalDevices); LOAD(GetPhysicalDeviceFeatures2);
    LOAD(GetPhysicalDeviceProperties2); LOAD(EnumerateDeviceExtensionProperties); LOAD(DestroyInstance);
    if (!EnumeratePhysicalDevices || !GetPhysicalDeviceFeatures2 || !GetPhysicalDeviceProperties2 ||
        !EnumerateDeviceExtensionProperties || !DestroyInstance) { LOG("FAIL missing instance entrypoint\n"); return 1; }
    uint32_t count = 0;
    rc = EnumeratePhysicalDevices(instance, &count, NULL);
    LOG("physicalDevices=%u result=%d\n", count, rc);
    if (rc != VK_SUCCESS || !count) { DestroyInstance(instance, NULL); return 1; }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) { DestroyInstance(instance, NULL); return 1; }
    rc = EnumeratePhysicalDevices(instance, &count, devices);
    if (rc != VK_SUCCESS) { LOG("FAIL enumerate=%d\n", rc); free(devices); DestroyInstance(instance, NULL); return 1; }
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceVulkan12Properties p12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
        VkPhysicalDeviceProperties2 props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p12 };
        GetPhysicalDeviceProperties2(devices[i], &props);
        LOG("device=%s api=%u.%u.%u driver=%u\n", props.properties.deviceName,
            VK_VERSION_MAJOR(props.properties.apiVersion), VK_VERSION_MINOR(props.properties.apiVersion),
            VK_VERSION_PATCH(props.properties.apiVersion), props.properties.driverVersion);
        #define LIMIT(f) LOG(#f "=%u\n", p12.f)
        LIMIT(maxUpdateAfterBindDescriptorsInAllPools); LIMIT(maxDescriptorSetUpdateAfterBindSamplers);
        LIMIT(maxDescriptorSetUpdateAfterBindSampledImages); LIMIT(maxDescriptorSetUpdateAfterBindStorageImages);
        LIMIT(maxDescriptorSetUpdateAfterBindStorageBuffers); LIMIT(maxDescriptorSetUpdateAfterBindUniformBuffers);
        VkPhysicalDeviceRobustness2FeaturesEXT robust = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
        VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT, .pNext = &robust };
        VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &interlock };
        VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13 };
        VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &f12 };
        VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f11 };
        GetPhysicalDeviceFeatures2(devices[i], &features);
        FEATURE(f11, shaderDrawParameters); FEATURE(f12, samplerMirrorClampToEdge);
        FEATURE(f12, descriptorIndexing); FEATURE(f12, runtimeDescriptorArray);
        FEATURE(f12, descriptorBindingPartiallyBound); FEATURE(f12, descriptorBindingVariableDescriptorCount);
        FEATURE(f12, descriptorBindingSampledImageUpdateAfterBind); FEATURE(f12, descriptorBindingStorageImageUpdateAfterBind);
        FEATURE(f12, descriptorBindingStorageBufferUpdateAfterBind); FEATURE(f12, descriptorBindingUniformTexelBufferUpdateAfterBind);
        FEATURE(f12, descriptorBindingStorageTexelBufferUpdateAfterBind); FEATURE(f12, shaderSampledImageArrayNonUniformIndexing);
        FEATURE(f12, shaderStorageImageArrayNonUniformIndexing); FEATURE(f12, shaderStorageBufferArrayNonUniformIndexing);
        FEATURE(f12, shaderUniformTexelBufferArrayNonUniformIndexing); FEATURE(f12, shaderStorageTexelBufferArrayNonUniformIndexing);
        FEATURE(f12, bufferDeviceAddress); FEATURE(f12, timelineSemaphore);
        FEATURE(f13, synchronization2); FEATURE(f13, dynamicRendering);
        FEATURE(interlock, fragmentShaderPixelInterlock); FEATURE(interlock, fragmentShaderSampleInterlock);
        FEATURE(robust, robustBufferAccess2); FEATURE(robust, robustImageAccess2); FEATURE(robust, nullDescriptor);
        uint32_t n = 0;
        rc = EnumerateDeviceExtensionProperties(devices[i], NULL, &n, NULL);
        if (rc != VK_SUCCESS) { LOG("extensions query failed=%d\n", rc); continue; }
        VkExtensionProperties *extensions = calloc(n, sizeof(*extensions));
        if (!extensions) continue;
        rc = EnumerateDeviceExtensionProperties(devices[i], NULL, &n, extensions);
        LOG("extensionCount=%u result=%d\n", n, rc);
        if (rc == VK_SUCCESS) for (uint32_t j = 0; j < n; ++j) LOG("extension=%s\n", extensions[j].extensionName);
        free(extensions);
    }
    free(devices);
    DestroyInstance(instance, NULL);
    LOG("PROBE_COMPLETE feature inventory only; D3D12 device/draw/present remain untested\n");
    if (output) fclose(output);
    return 0;
}
