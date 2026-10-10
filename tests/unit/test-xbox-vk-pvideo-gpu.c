/*
 * Real Vulkan PVIDEO resource lifecycle and fixed-work allocation benchmark.
 * Opt in with XEMU_TEST_VK_PVIDEO=1. --benchmark records the production path
 * without enforcing the reuse gate, so an unfixed reference can be measured.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

static unsigned image_creates, view_creates, sampler_creates;

static VkResult counted_image(VmaAllocator allocator,
                              const VkImageCreateInfo *info,
                              const VmaAllocationCreateInfo *allocation_info,
                              VkImage *image, VmaAllocation *allocation,
                              VmaAllocationInfo *result_info)
{
    image_creates++;
    return vmaCreateImage(allocator, info, allocation_info, image, allocation,
                          result_info);
}

static VkResult counted_view(VkDevice device, const VkImageViewCreateInfo *info,
                             const VkAllocationCallbacks *allocator,
                             VkImageView *view)
{
    view_creates++;
    return vkCreateImageView(device, info, allocator, view);
}

static VkResult counted_sampler(VkDevice device,
                                const VkSamplerCreateInfo *info,
                                const VkAllocationCallbacks *allocator,
                                VkSampler *sampler)
{
    sampler_creates++;
    return vkCreateSampler(device, info, allocator, sampler);
}

#define vmaCreateImage counted_image
#define vkCreateImageView counted_view
#define vkCreateSampler counted_sampler
#ifndef XEMU_PVIDEO_DISPLAY_SOURCE
#define XEMU_PVIDEO_DISPLAY_SOURCE "hw/xbox/nv2a/pgraph/vk/display.c"
#endif
#include XEMU_PVIDEO_DISPLAY_SOURCE

int main(int argc, char **argv)
{
    const char *enabled = getenv("XEMU_TEST_VK_PVIDEO");
    if (!enabled || strcmp(enabled, "1")) {
        puts("1..0 # SKIP set XEMU_TEST_VK_PVIDEO=1 on a Vulkan host");
        return EXIT_SUCCESS;
    }
    bool benchmark = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--benchmark")) {
            benchmark = true;
        } else if (strcmp(argv[i], "--tap") && strcmp(argv[i], "-k")) {
            fprintf(stderr, "Usage: %s [--benchmark] [--tap] [-k]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    VK_CHECK(volkInitialize());
    VkInstance instance;
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "xemu-pvideo-resource-test",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    volkLoadInstance(instance);
    uint32_t device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &device_count, NULL));
    g_assert_cmpuint(device_count, >, 0);
    VkPhysicalDevice *devices = g_new(VkPhysicalDevice, device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &device_count, devices));
    VkPhysicalDevice physical = devices[0];
    g_free(devices);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
    VkQueueFamilyProperties *families =
        g_new(VkQueueFamilyProperties, family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
    uint32_t family;
    for (family = 0; family < family_count; family++) {
        if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            break;
        }
    }
    g_free(families);
    g_assert_cmpuint(family, <, family_count);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &r->device));
    volkLoadDevice(r->device);
    VmaVulkanFunctions functions;
    VmaAllocatorCreateInfo allocator_info = {
        .physicalDevice = physical,
        .device = r->device,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_1,
        .pVulkanFunctions = &functions,
    };
    VK_CHECK(vmaImportVulkanFunctionsFromVolk(&allocator_info, &functions));
    VK_CHECK(vmaCreateAllocator(&allocator_info, &r->allocator));

    /* Warm driver/allocator setup outside the measured fixed-work batch. */
    create_pvideo_image(pg, 640, 480);
    destroy_pvideo_image(pg);
    image_creates = view_creates = sampler_creates = 0;
    int64_t start_us = g_get_monotonic_time();
    for (unsigned i = 0; i < 10001; i++) {
        create_pvideo_image(pg, 640, 480);
    }
    int64_t elapsed_us = g_get_monotonic_time() - start_us;
    VmaTotalStatistics stats;
    vmaCalculateStatistics(r->allocator, &stats);
    g_assert_cmpuint(stats.total.statistics.allocationCount, ==, 1);
    unsigned steady_creates = image_creates;
    if (!benchmark) {
        g_assert_cmpuint(image_creates, ==, 1);
        g_assert_cmpuint(view_creates, ==, 1);
        g_assert_cmpuint(sampler_creates, ==, 1);
    }

    /* Real driver replacement, teardown and recreation remain balanced. */
    for (unsigned i = 0; i < 1000; i++) {
        create_pvideo_image(pg, 640 + (i % 2) * 2, 480);
        create_pvideo_image(pg, 640 + (i % 2) * 2, 480);
        if (i % 10 == 0) {
            destroy_pvideo_image(pg);
        }
    }
    destroy_pvideo_image(pg);
    vmaCalculateStatistics(r->allocator, &stats);
    g_assert_cmpuint(stats.total.statistics.allocationCount, ==, 0);
    printf("# device=%s\n# driver_version=%u\n", properties.deviceName,
           properties.driverVersion);
    printf("# requests=10001\n# image_creates=%u\n# elapsed_us=%" PRId64 "\n",
           steady_creates, elapsed_us);
    printf("# ns_per_request=%.3f\n# live_allocations_after_teardown=0\n",
           elapsed_us * 1000.0 / 10001);

    vmaDestroyAllocator(r->allocator);
    vkDestroyDevice(r->device, NULL);
    vkDestroyInstance(instance, NULL);
    g_free(r);
    g_free(pg);
    puts("1..1\nok 1 - real Vulkan PVIDEO resource lifecycle");
    return EXIT_SUCCESS;
}
