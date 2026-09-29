/*
 * Versioned ABI for optional host neural-presentation adapters.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_PLUGIN_H
#define HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XEMU_NEURAL_PLUGIN_ABI_VERSION 2U
#define XEMU_NEURAL_PLUGIN_ENTRYPOINT "xemu_neural_present_get_api"
#define XEMU_NEURAL_PLUGIN_STATUS_TEXT_SIZE 192U
#define XEMU_NEURAL_PLUGIN_MAX_REQUIREMENT_COUNT 64U

#if defined(_WIN32)
#define XEMU_NEURAL_PLUGIN_EXPORT __declspec(dllexport)
#else
#define XEMU_NEURAL_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef uint32_t XemuNeuralPluginResult;
enum {
    XEMU_NEURAL_PLUGIN_RESULT_OK = 0,
    XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH = 1,
    XEMU_NEURAL_PLUGIN_RESULT_BYPASS = 2,
    XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR = 3,
    XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR = 4,
};

typedef uint32_t XemuNeuralPluginState;
enum {
    XEMU_NEURAL_PLUGIN_STATE_UNINITIALIZED = 0,
    XEMU_NEURAL_PLUGIN_STATE_READY = 1,
    XEMU_NEURAL_PLUGIN_STATE_ACTIVE = 2,
    XEMU_NEURAL_PLUGIN_STATE_BYPASSED = 3,
    XEMU_NEURAL_PLUGIN_STATE_FAILED = 4,
};

typedef uint32_t XemuNeuralPluginFeature;
enum {
    XEMU_NEURAL_PLUGIN_FEATURE_NONE = 0,
    XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH = 1,
    XEMU_NEURAL_PLUGIN_FEATURE_DLAA = 2,
    XEMU_NEURAL_PLUGIN_FEATURE_DLSS_SUPER_RESOLUTION = 3,
    XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR = 4,
};

typedef uint64_t XemuNeuralPluginCapabilities;
enum {
    /* record_vulkan records work into the command buffer supplied by xemu. */
    XEMU_NEURAL_PLUGIN_CAP_VULKAN_RECORD = UINT64_C(1) << 0,
    /* Error returns cannot leave a partially overwritten presentation image. */
    XEMU_NEURAL_PLUGIN_CAP_TRANSACTIONAL_IN_PLACE = UINT64_C(1) << 1,
    /* The adapter constructs or estimates motion internally. */
    XEMU_NEURAL_PLUGIN_CAP_INTERNAL_MOTION = UINT64_C(1) << 2,
    /* The adapter can run when no depth resource is supplied. */
    XEMU_NEURAL_PLUGIN_CAP_DEPTH_OPTIONAL = UINT64_C(1) << 3,
    /* The adapter refuses frames without a validated depth resource. */
    XEMU_NEURAL_PLUGIN_CAP_REQUIRES_DEPTH = UINT64_C(1) << 4,
    /* The adapter intentionally implements a color-only contract. */
    XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY = UINT64_C(1) << 5,
    /* Adapter supplies pre-device vkCreateInstance/vkCreateDevice proxies. */
    XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY = UINT64_C(1) << 6,
    /* Explicitly promises no neural inference; used by Adapter Validation. */
    XEMU_NEURAL_PLUGIN_CAP_VALIDATION_ONLY = UINT64_C(1) << 7,
};

typedef uint32_t XemuNeuralPluginFrameFlags;
enum {
    XEMU_NEURAL_PLUGIN_FRAME_RESET_HISTORY = 1U << 0,
    XEMU_NEURAL_PLUGIN_FRAME_HAS_DEPTH = 1U << 1,
    XEMU_NEURAL_PLUGIN_FRAME_COLOR_SRGB = 1U << 2,
    XEMU_NEURAL_PLUGIN_FRAME_REPEATED_SOURCE = 1U << 3,
};

typedef uint32_t XemuNeuralPluginOutputFlags;
enum {
    XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN = 1U << 0,
    XEMU_NEURAL_PLUGIN_OUTPUT_HISTORY_VALID = 1U << 1,
};

typedef uint32_t XemuNeuralPluginLogLevel;
enum {
    XEMU_NEURAL_PLUGIN_LOG_DEBUG = 0,
    XEMU_NEURAL_PLUGIN_LOG_INFO = 1,
    XEMU_NEURAL_PLUGIN_LOG_WARNING = 2,
    XEMU_NEURAL_PLUGIN_LOG_ERROR = 3,
};

typedef void (*XemuNeuralPluginLogFn)(
    void *opaque, XemuNeuralPluginLogLevel level, const char *message);

typedef struct XemuNeuralPluginHostV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void *opaque;
    XemuNeuralPluginLogFn log;
} XemuNeuralPluginHostV1;

#define XEMU_NEURAL_PLUGIN_HOST_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginHostV1), \
      .abi_version = XEMU_NEURAL_PLUGIN_ABI_VERSION }

typedef struct XemuNeuralPluginVulkanDeviceV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uintptr_t instance;
    uintptr_t physical_device;
    uintptr_t device;
    uintptr_t queue;
    uint32_t queue_family_index;
    uint32_t queue_flags;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t driver_version;
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
} XemuNeuralPluginVulkanDeviceV1;

#define XEMU_NEURAL_PLUGIN_VULKAN_DEVICE_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginVulkanDeviceV1) }

/*
 * Requirements returned before xemu creates its Vulkan instance/device.
 * String arrays remain owned by the adapter and must stay valid until the
 * matching shutdown callback. Vulkan feature names use the exact member names
 * from VkPhysicalDeviceVulkan12Features/Vulkan13Features.
 */
typedef struct XemuNeuralPluginVulkanRequirementsV1 {
    uint32_t struct_size;
    uint32_t minimum_api_version;

    uint32_t instance_extension_count;
    const char *const *instance_extensions;
    uint32_t device_extension_count;
    const char *const *device_extensions;

    uint32_t feature12_count;
    const char *const *feature12_names;
    uint32_t feature13_count;
    const char *const *feature13_names;

    /* Additional queues required beyond the queues already created by xemu. */
    uint32_t additional_graphics_queue_count;
    uint32_t additional_compute_queue_count;
    uint32_t additional_optical_flow_queue_count;
    uint32_t reserved;
} XemuNeuralPluginVulkanRequirementsV1;

#define XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginVulkanRequirementsV1) }

typedef struct XemuNeuralPluginCreateInfoV1 {
    uint32_t struct_size;
    uint32_t reserved;
    const XemuNeuralPluginHostV1 *host;
    void *bootstrap_context;
    XemuNeuralPluginVulkanDeviceV1 vulkan;
} XemuNeuralPluginCreateInfoV1;

#define XEMU_NEURAL_PLUGIN_CREATE_INFO_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginCreateInfoV1), \
      .vulkan = XEMU_NEURAL_PLUGIN_VULKAN_DEVICE_V1_INIT }

typedef struct XemuNeuralPluginStatusV1 {
    uint32_t struct_size;
    XemuNeuralPluginState state;
    XemuNeuralPluginFeature active_feature;
    uint32_t reserved;
    uint64_t evaluated_frames;
    uint64_t bypassed_frames;
    uint64_t failed_frames;
    char text[XEMU_NEURAL_PLUGIN_STATUS_TEXT_SIZE];
} XemuNeuralPluginStatusV1;

#define XEMU_NEURAL_PLUGIN_STATUS_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginStatusV1) }

typedef struct XemuNeuralPluginFrameV1 {
    uint32_t struct_size;
    XemuNeuralPluginFrameFlags flags;
    uint64_t frame_id;
    uint64_t reset_epoch;
    uint64_t surface_lifetime_id;
    uint64_t output_generation;
    int64_t guest_frame_time;
    uint64_t scanout_address;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t processing_width;
    uint32_t processing_height;
    int32_t active_x;
    int32_t active_y;
    uint32_t active_width;
    uint32_t active_height;

    /* Vulkan values are encoded numerically to keep this header SDK-neutral. */
    uint64_t color_image;
    uint64_t color_image_view;
    uint32_t color_format;
    uint32_t color_layout;
    uint32_t required_return_layout;
    uint32_t color_sample_count;

    uint64_t depth_image;
    uint64_t depth_image_view;
    uint32_t depth_format;
    uint32_t depth_layout;
    uint32_t depth_width;
    uint32_t depth_height;
} XemuNeuralPluginFrameV1;

#define XEMU_NEURAL_PLUGIN_FRAME_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginFrameV1) }

typedef struct XemuNeuralPluginVulkanRecordV1 {
    uint32_t struct_size;
    uint32_t reserved;
    uintptr_t command_buffer;
    XemuNeuralPluginFrameV1 frame;
} XemuNeuralPluginVulkanRecordV1;

#define XEMU_NEURAL_PLUGIN_VULKAN_RECORD_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginVulkanRecordV1), \
      .frame = XEMU_NEURAL_PLUGIN_FRAME_V1_INIT }

typedef struct XemuNeuralPluginFrameOutputV1 {
    uint32_t struct_size;
    XemuNeuralPluginOutputFlags flags;
    XemuNeuralPluginFeature feature;
    uint32_t reserved;
    uint64_t gpu_time_ns;
    char text[XEMU_NEURAL_PLUGIN_STATUS_TEXT_SIZE];
} XemuNeuralPluginFrameOutputV1;

#define XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT \
    { .struct_size = sizeof(XemuNeuralPluginFrameOutputV1) }

typedef uint32_t XemuNeuralPluginFrameOutputValidation;
enum {
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_VALID = 0,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_NULL = 1,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_SIZE = 2,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_FLAGS = 3,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_FEATURE = 4,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_MISSING_COLOR = 5,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_MISSING_FEATURE = 6,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT = 7,
    XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_RESULT = 8,
};

static inline XemuNeuralPluginFrameOutputValidation
xemu_neural_plugin_frame_output_validate(
    XemuNeuralPluginResult result,
    const XemuNeuralPluginFrameOutputV1 *output)
{
    const uint32_t known_flags =
        XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN |
        XEMU_NEURAL_PLUGIN_OUTPUT_HISTORY_VALID;

    if (output == NULL) {
        return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_NULL;
    }
    if (output->struct_size < sizeof(*output)) {
        return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_SIZE;
    }
    if (output->flags & ~known_flags) {
        return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_FLAGS;
    }
    if (output->feature > XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR) {
        return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_FEATURE;
    }

    if (result > XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR) {
        return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_RESULT;
    }

    if (result == XEMU_NEURAL_PLUGIN_RESULT_OK) {
        if (!(output->flags & XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN)) {
            return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_MISSING_COLOR;
        }
        if (output->feature == XEMU_NEURAL_PLUGIN_FEATURE_NONE) {
            return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_MISSING_FEATURE;
        }
    } else if (result == XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH ||
               result == XEMU_NEURAL_PLUGIN_RESULT_BYPASS) {
        if (output->flags != 0 ||
            (output->feature != XEMU_NEURAL_PLUGIN_FEATURE_NONE &&
             output->feature != XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH)) {
            return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT;
        }
    } else if (output->flags != 0 ||
               output->feature != XEMU_NEURAL_PLUGIN_FEATURE_NONE) {
        return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT;
    }
    return XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_VALID;
}

static inline const char *xemu_neural_plugin_frame_output_validation_string(
    XemuNeuralPluginFrameOutputValidation validation)
{
    switch (validation) {
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_VALID:
        return "valid";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_NULL:
        return "null frame output";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_SIZE:
        return "frame output structure is too small";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_FLAGS:
        return "frame output contains unknown flags";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_FEATURE:
        return "frame output names an unknown feature";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_MISSING_COLOR:
        return "successful output did not write color";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_MISSING_FEATURE:
        return "successful output did not name a feature";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT:
        return "frame output conflicts with the returned result";
    case XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_RESULT:
        return "adapter returned an unknown result";
    }
    return "unknown frame output validation result";
}

typedef XemuNeuralPluginResult (*XemuNeuralPluginCreateFn)(
    const XemuNeuralPluginCreateInfoV1 *create_info, void **context,
    XemuNeuralPluginStatusV1 *status);
typedef void (*XemuNeuralPluginDestroyFn)(void *context);
typedef XemuNeuralPluginResult (*XemuNeuralPluginPrepareFrameFn)(
    void *context, const XemuNeuralPluginFrameV1 *frame,
    XemuNeuralPluginStatusV1 *status);
typedef XemuNeuralPluginResult (*XemuNeuralPluginRecordVulkanFn)(
    void *context, const XemuNeuralPluginVulkanRecordV1 *record,
    XemuNeuralPluginFrameOutputV1 *output);
typedef void (*XemuNeuralPluginResetHistoryFn)(void *context, uint32_t reason);
typedef void (*XemuNeuralPluginReleaseResourcesFn)(void *context);
typedef void (*XemuNeuralPluginGetStatusFn)(
    void *context, XemuNeuralPluginStatusV1 *status);
typedef XemuNeuralPluginResult (*XemuNeuralPluginBootstrapFn)(
    const XemuNeuralPluginHostV1 *host, void **bootstrap_context,
    XemuNeuralPluginVulkanRequirementsV1 *requirements,
    XemuNeuralPluginStatusV1 *status);
typedef void (*XemuNeuralPluginShutdownFn)(void *bootstrap_context);
typedef int32_t (*XemuNeuralPluginCreateVulkanInstanceFn)(
    void *bootstrap_context, const void *create_info,
    const void *allocation_callbacks, uintptr_t *instance_out);
typedef int32_t (*XemuNeuralPluginCreateVulkanDeviceFn)(
    void *bootstrap_context, uintptr_t physical_device,
    const void *create_info, const void *allocation_callbacks,
    uintptr_t *device_out);

typedef struct XemuNeuralPluginApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    XemuNeuralPluginCapabilities capabilities;
    const char *name;
    const char *version;
    XemuNeuralPluginBootstrapFn bootstrap;
    XemuNeuralPluginShutdownFn shutdown;
    XemuNeuralPluginCreateVulkanInstanceFn create_vulkan_instance;
    XemuNeuralPluginCreateVulkanDeviceFn create_vulkan_device;
    XemuNeuralPluginCreateFn create;
    XemuNeuralPluginDestroyFn destroy;
    XemuNeuralPluginPrepareFrameFn prepare_frame;
    XemuNeuralPluginRecordVulkanFn record_vulkan;
    XemuNeuralPluginResetHistoryFn reset_history;
    XemuNeuralPluginReleaseResourcesFn release_resources;
    XemuNeuralPluginGetStatusFn get_status;
} XemuNeuralPluginApiV1;

typedef const XemuNeuralPluginApiV1 *(*XemuNeuralPluginGetApiFn)(
    uint32_t requested_abi_version);

typedef uint32_t XemuNeuralPluginApiValidation;
enum {
    XEMU_NEURAL_PLUGIN_API_VALID = 0,
    XEMU_NEURAL_PLUGIN_API_NULL = 1,
    XEMU_NEURAL_PLUGIN_API_BAD_VERSION = 2,
    XEMU_NEURAL_PLUGIN_API_BAD_SIZE = 3,
    XEMU_NEURAL_PLUGIN_API_BAD_IDENTITY = 4,
    XEMU_NEURAL_PLUGIN_API_MISSING_CAPABILITY = 5,
    XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK = 6,
    XEMU_NEURAL_PLUGIN_API_CONFLICTING_CAPABILITY = 7,
    XEMU_NEURAL_PLUGIN_API_MISSING_FRAME_CONTRACT = 8,
};

typedef uint32_t XemuNeuralPluginRequirementsValidation;
enum {
    XEMU_NEURAL_PLUGIN_REQUIREMENTS_VALID = 0,
    XEMU_NEURAL_PLUGIN_REQUIREMENTS_NULL = 1,
    XEMU_NEURAL_PLUGIN_REQUIREMENTS_BAD_SIZE = 2,
    XEMU_NEURAL_PLUGIN_REQUIREMENTS_TOO_MANY = 3,
    XEMU_NEURAL_PLUGIN_REQUIREMENTS_MISSING_LIST = 4,
    XEMU_NEURAL_PLUGIN_REQUIREMENTS_BAD_NAME = 5,
    XEMU_NEURAL_PLUGIN_REQUIREMENTS_QUEUE_COUNT = 6,
};

static inline bool xemu_neural_plugin_string_list_valid(
    uint32_t count, const char *const *items)
{
    if (count == 0) {
        return true;
    }
    if (items == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i] == NULL || items[i][0] == '\0') {
            return false;
        }
    }
    return true;
}

static inline XemuNeuralPluginRequirementsValidation
xemu_neural_plugin_requirements_validate(
    const XemuNeuralPluginVulkanRequirementsV1 *requirements)
{
    if (requirements == NULL) {
        return XEMU_NEURAL_PLUGIN_REQUIREMENTS_NULL;
    }
    if (requirements->struct_size < sizeof(*requirements)) {
        return XEMU_NEURAL_PLUGIN_REQUIREMENTS_BAD_SIZE;
    }
    if (requirements->instance_extension_count >
            XEMU_NEURAL_PLUGIN_MAX_REQUIREMENT_COUNT ||
        requirements->device_extension_count >
            XEMU_NEURAL_PLUGIN_MAX_REQUIREMENT_COUNT ||
        requirements->feature12_count >
            XEMU_NEURAL_PLUGIN_MAX_REQUIREMENT_COUNT ||
        requirements->feature13_count >
            XEMU_NEURAL_PLUGIN_MAX_REQUIREMENT_COUNT) {
        return XEMU_NEURAL_PLUGIN_REQUIREMENTS_TOO_MANY;
    }
    if ((requirements->instance_extension_count != 0 &&
         requirements->instance_extensions == NULL) ||
        (requirements->device_extension_count != 0 &&
         requirements->device_extensions == NULL) ||
        (requirements->feature12_count != 0 &&
         requirements->feature12_names == NULL) ||
        (requirements->feature13_count != 0 &&
         requirements->feature13_names == NULL)) {
        return XEMU_NEURAL_PLUGIN_REQUIREMENTS_MISSING_LIST;
    }
    if (!xemu_neural_plugin_string_list_valid(
            requirements->instance_extension_count,
            requirements->instance_extensions) ||
        !xemu_neural_plugin_string_list_valid(
            requirements->device_extension_count,
            requirements->device_extensions) ||
        !xemu_neural_plugin_string_list_valid(
            requirements->feature12_count,
            requirements->feature12_names) ||
        !xemu_neural_plugin_string_list_valid(
            requirements->feature13_count,
            requirements->feature13_names)) {
        return XEMU_NEURAL_PLUGIN_REQUIREMENTS_BAD_NAME;
    }
    if (requirements->additional_graphics_queue_count > 16 ||
        requirements->additional_compute_queue_count > 16 ||
        requirements->additional_optical_flow_queue_count > 16) {
        return XEMU_NEURAL_PLUGIN_REQUIREMENTS_QUEUE_COUNT;
    }
    return XEMU_NEURAL_PLUGIN_REQUIREMENTS_VALID;
}

static inline const char *xemu_neural_plugin_requirements_validation_string(
    XemuNeuralPluginRequirementsValidation validation)
{
    switch (validation) {
    case XEMU_NEURAL_PLUGIN_REQUIREMENTS_VALID:
        return "valid";
    case XEMU_NEURAL_PLUGIN_REQUIREMENTS_NULL:
        return "null requirements";
    case XEMU_NEURAL_PLUGIN_REQUIREMENTS_BAD_SIZE:
        return "requirements structure is too small";
    case XEMU_NEURAL_PLUGIN_REQUIREMENTS_TOO_MANY:
        return "requirements list exceeds the ABI limit";
    case XEMU_NEURAL_PLUGIN_REQUIREMENTS_MISSING_LIST:
        return "requirements count has no matching list";
    case XEMU_NEURAL_PLUGIN_REQUIREMENTS_BAD_NAME:
        return "requirements contain an empty name";
    case XEMU_NEURAL_PLUGIN_REQUIREMENTS_QUEUE_COUNT:
        return "requirements request an unreasonable queue count";
    }
    return "unknown requirements validation result";
}

static inline XemuNeuralPluginApiValidation
xemu_neural_plugin_api_validate(const XemuNeuralPluginApiV1 *api)
{
    const uint64_t required = XEMU_NEURAL_PLUGIN_CAP_VULKAN_RECORD |
                              XEMU_NEURAL_PLUGIN_CAP_TRANSACTIONAL_IN_PLACE;
    const size_t minimum_size =
        offsetof(XemuNeuralPluginApiV1, get_status) + sizeof(api->get_status);

    if (api == NULL) {
        return XEMU_NEURAL_PLUGIN_API_NULL;
    }
    if (api->abi_version != XEMU_NEURAL_PLUGIN_ABI_VERSION) {
        return XEMU_NEURAL_PLUGIN_API_BAD_VERSION;
    }
    if (api->struct_size < minimum_size) {
        return XEMU_NEURAL_PLUGIN_API_BAD_SIZE;
    }
    if (api->name == NULL || api->name[0] == '\0' ||
        api->version == NULL || api->version[0] == '\0') {
        return XEMU_NEURAL_PLUGIN_API_BAD_IDENTITY;
    }
    if ((api->capabilities & required) != required) {
        return XEMU_NEURAL_PLUGIN_API_MISSING_CAPABILITY;
    }

    const bool internal_motion =
        api->capabilities & XEMU_NEURAL_PLUGIN_CAP_INTERNAL_MOTION;
    const bool depth_optional =
        api->capabilities & XEMU_NEURAL_PLUGIN_CAP_DEPTH_OPTIONAL;
    const bool requires_depth =
        api->capabilities & XEMU_NEURAL_PLUGIN_CAP_REQUIRES_DEPTH;
    const bool color_only =
        api->capabilities & XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY;
    const bool create_proxy =
        api->capabilities & XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY;
    unsigned int depth_modes = (unsigned int)depth_optional +
                               (unsigned int)requires_depth +
                               (unsigned int)color_only;

    /* ABI v1 has no host-supplied motion resource. */
    if (!internal_motion && !color_only) {
        return XEMU_NEURAL_PLUGIN_API_MISSING_FRAME_CONTRACT;
    }
    if (depth_modes != 1) {
        return XEMU_NEURAL_PLUGIN_API_CONFLICTING_CAPABILITY;
    }
    if ((create_proxy && (api->create_vulkan_instance == NULL ||
                          api->create_vulkan_device == NULL)) ||
        (!create_proxy && (api->create_vulkan_instance != NULL ||
                           api->create_vulkan_device != NULL))) {
        return XEMU_NEURAL_PLUGIN_API_CONFLICTING_CAPABILITY;
    }
    if (api->bootstrap == NULL || api->shutdown == NULL ||
        api->create == NULL || api->destroy == NULL ||
        api->prepare_frame == NULL || api->record_vulkan == NULL ||
        api->reset_history == NULL || api->release_resources == NULL ||
        api->get_status == NULL) {
        return XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK;
    }
    return XEMU_NEURAL_PLUGIN_API_VALID;
}

static inline const char *xemu_neural_plugin_api_validation_string(
    XemuNeuralPluginApiValidation validation)
{
    switch (validation) {
    case XEMU_NEURAL_PLUGIN_API_VALID:
        return "valid";
    case XEMU_NEURAL_PLUGIN_API_NULL:
        return "null API";
    case XEMU_NEURAL_PLUGIN_API_BAD_VERSION:
        return "unsupported ABI version";
    case XEMU_NEURAL_PLUGIN_API_BAD_SIZE:
        return "API structure is too small";
    case XEMU_NEURAL_PLUGIN_API_BAD_IDENTITY:
        return "missing plug-in name or version";
    case XEMU_NEURAL_PLUGIN_API_MISSING_CAPABILITY:
        return "required Vulkan recording capability is missing";
    case XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK:
        return "required callback is missing";
    case XEMU_NEURAL_PLUGIN_API_CONFLICTING_CAPABILITY:
        return "conflicting capabilities";
    case XEMU_NEURAL_PLUGIN_API_MISSING_FRAME_CONTRACT:
        return "adapter has no usable motion/color frame contract";
    }
    return "unknown validation result";
}

#ifdef __cplusplus
}
#endif

#endif
