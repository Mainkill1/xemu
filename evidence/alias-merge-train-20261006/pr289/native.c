/* Scoped native qualification of the actual surface-compute.c adapter.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include <shaderc/shaderc.h>

static VkPhysicalDevice physical;
static VkQueue queue;
static VkCommandPool command_pool;
static unsigned shader_compiles;

static unsigned validation_errors;
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *opaque)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        validation_errors++;
        fprintf(stderr, "VALIDATION: %s\n", data->pMessage);
    }
    return VK_FALSE;
}


uint64_t fast_hash(const uint8_t *bytes, size_t size)
{
    uint64_t value = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < size; i++) {
        value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return value;
}

void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                 float color[4], const char *format, ...)
{
}

void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd)
{
}

ShaderModuleInfo *pgraph_vk_create_shader_module_from_glsl(
    PGRAPHVkState *r, VkShaderStageFlagBits kind, const char *glsl)
{
    assert(kind == VK_SHADER_STAGE_COMPUTE_BIT);
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, glsl, strlen(glsl), shaderc_compute_shader,
        "production-surface-compute.glsl", "main", NULL);
    if (shaderc_result_get_compilation_status(result) !=
        shaderc_compilation_status_success) {
        fprintf(stderr, "%s", shaderc_result_get_error_message(result));
        abort();
    }
    ShaderModuleInfo *module = g_new0(ShaderModuleInfo, 1);
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderc_result_get_length(result),
        .pCode = (const uint32_t *)shaderc_result_get_bytes(result),
    };
    VK_CHECK(vkCreateShaderModule(r->device, &info, NULL, &module->module));
    shaderc_result_release(result);
    shaderc_compiler_release(compiler);
    shader_compiles++;
    return module;
}

void pgraph_vk_destroy_shader_module(PGRAPHVkState *r, ShaderModuleInfo *module)
{
    vkDestroyShaderModule(r->device, module->module, NULL);
    g_free(module);
}

typedef struct ComputeFixtureBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t *bytes;
} ComputeFixtureBuffer;

static ComputeFixtureBuffer create_buffer(PGRAPHVkState *r, size_t bytes)
{
    ComputeFixtureBuffer buffer = { 0 };
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VK_CHECK(vkCreateBuffer(r->device, &info, NULL, &buffer.buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(r->device, buffer.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    unsigned type;
    for (type = 0; type < properties.memoryTypeCount; type++) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[type].propertyFlags;
        if ((requirements.memoryTypeBits & (1u << type)) &&
            (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            break;
        }
    }
    assert(type < properties.memoryTypeCount);
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type,
    };
    VK_CHECK(vkAllocateMemory(r->device, &allocation, NULL, &buffer.memory));
    VK_CHECK(vkBindBufferMemory(r->device, buffer.buffer, buffer.memory, 0));
    VK_CHECK(vkMapMemory(r->device, buffer.memory, 0, bytes, 0,
                        (void **)&buffer.bytes));
    memset(buffer.bytes, 0, bytes);
    return buffer;
}

static void destroy_buffer(PGRAPHVkState *r, ComputeFixtureBuffer *buffer)
{
    vkUnmapMemory(r->device, buffer->memory);
    vkDestroyBuffer(r->device, buffer->buffer, NULL);
    vkFreeMemory(r->device, buffer->memory, NULL);
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

#include "native-alias.inc"

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    VK_CHECK(volkInitialize());
    const char *layer="VK_LAYER_KHRONOS_validation", *ext=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    uint32_t n=0; vkEnumerateInstanceLayerProperties(&n,NULL);
    VkLayerProperties *layers=g_new0(VkLayerProperties,n);vkEnumerateInstanceLayerProperties(&n,layers);
    bool validation=false;for(unsigned i=0;i<n;i++)validation|=!strcmp(layers[i].layerName,layer);g_free(layers);
    VkDebugUtilsMessengerCreateInfoEXT dbg={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,.pfnUserCallback=debug_message};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledLayerCount=validation,.ppEnabledLayerNames=&layer,
        .enabledExtensionCount=validation,.ppEnabledExtensionNames=&ext,.pNext=validation?&dbg:NULL};
    VkInstance instance;VK_CHECK(vkCreateInstance(&ii,NULL,&instance));volkLoadInstance(instance);
    VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
    if(validation)VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance,&dbg,NULL,&messenger));
    uint32_t count = 1;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, &physical);
    assert(result == VK_SUCCESS || result == VK_INCOMPLETE);
    assert(count);
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    vkGetPhysicalDeviceProperties(physical, &r->device_props);
    VkPhysicalDeviceProperties actual = r->device_props;
    printf("device=%s actualX=%u actualInv=%u\n", actual.deviceName,
        actual.limits.maxComputeWorkGroupSize[0],
        actual.limits.maxComputeWorkGroupInvocations);
    printf("vendor=%04x device=%04x api=%u driver=%u type=%u\n",
        actual.vendorID, actual.deviceID, actual.apiVersion,
        actual.driverVersion, actual.deviceType);
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
    VkQueueFamilyProperties *families = g_new0(VkQueueFamilyProperties, family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
    uint32_t family;
    for (family = 0; family < family_count; family++) {
        if (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            break;
        }
    }
    assert(family < family_count);
    g_free(families);
    float priority = 1;
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
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &r->device));
    volkLoadDevice(r->device);
    vkGetDeviceQueue(r->device, family, 0, &queue);
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family,
    };
    VK_CHECK(vkCreateCommandPool(r->device, &pool_info, NULL, &command_pool));
    pgraph_vk_init_compute(pg);
    r->device_props = actual;
    run_native_alias_cases(pg);
    pgraph_vk_finalize_compute(pg);
    vkDestroyCommandPool(r->device, command_pool, NULL);
    vkDestroyDevice(r->device, NULL);
    if(validation)vkDestroyDebugUtilsMessengerEXT(instance,messenger,NULL);
    vkDestroyInstance(instance, NULL);
    printf("RESULT validation=%s errors=%u\n",validation?"enabled":"unavailable",validation_errors);assert(!validation_errors);
    g_free(r);
    g_free(pg);

    return 0;
}
