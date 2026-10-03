/*
 * Outline for a Windows x64 NVIDIA Streamline 2.14+ DLSS NR adapter.
 *
 * This file is intentionally not part of the reference-adapter build. The
 * public Streamline 2.14.1 repository exposes kFeatureDLSS_NR and uplift
 * buffer tags, but no public sl_dlss_nr.h/programming guide. Complete the
 * guarded feature-contract section only with the exact NVIDIA package and
 * runtime being validated.
 *
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32)
#error "The initial Streamline adapter target is Windows x64"
#endif

#include <sl_security.h>
#include <windows.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include <sl.h>
#include <sl_core_api.h>
#include <sl_helpers_vk.h>

#include "hw/xbox/nv2a/pgraph/neural-present-plugin.h"

namespace {

struct SlExports {
    HMODULE module{};
    PFun_slInit *init{};
    PFun_slShutdown *shutdown{};
    PFun_slGetFeatureRequirements *get_feature_requirements{};
    PFun_slIsFeatureSupported *is_feature_supported{};
    PFun_slGetNewFrameToken *get_new_frame_token{};
    PFun_slSetTagForFrame *set_tag_for_frame{};
    PFun_slEvaluateFeature *evaluate_feature{};
    PFN_vkGetInstanceProcAddr get_instance_proc_addr{};
    PFN_vkGetDeviceProcAddr get_device_proc_addr{};
};

struct BootstrapContext {
    XemuNeuralPluginHostV1 host{};
    SlExports sl{};
    sl::FeatureRequirements requirements{};
    PFN_vkCreateInstance create_instance{};
    PFN_vkCreateDevice create_device{};
    VkInstance created_instance{};
    std::vector<const char *> instance_extensions;
    std::vector<const char *> device_extensions;
    std::vector<const char *> feature12_names;
    std::vector<const char *> feature13_names;
    std::wstring streamline_directory;
    std::wstring log_directory;
    std::string project_id;
    std::array<const wchar_t *, 1> plugin_paths{};
};

struct RuntimeContext {
    BootstrapContext *bootstrap{};
    VkInstance instance{};
    VkPhysicalDevice physical_device{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t queue_family{};
    sl::ViewportHandle viewport{1};
    uint32_t frame_index{};

    /*
     * A production implementation owns at least two output/history slots.
     * Each slot contains adapter-owned Vulkan images/views, allocation,
     * layout state, and retirement synchronization. Never evaluate directly
     * in-place over xemu's only original display image.
     */
};

template <typename T>
T load_export(HMODULE module, const char *name)
{
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

void copy_names(std::vector<const char *> &destination,
                uint32_t count, const char *const *source)
{
    destination.clear();
    if (count != 0 && source != nullptr) {
        destination.assign(source, source + count);
    }
}

bool is_guid_text(const char *value)
{
    if (value == nullptr || std::strlen(value) != 36) {
        return false;
    }
    for (size_t i = 0; i < 36; ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        const char c = value[i];
        if (separator ? c != '-' : !std::isxdigit(
                static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

bool make_absolute_path(const wchar_t *input, std::wstring &output)
{
    if (input == nullptr || input[0] == L'\0') {
        return false;
    }
    const DWORD required = GetFullPathNameW(input, 0, nullptr, nullptr);
    if (required == 0) {
        return false;
    }
    std::vector<wchar_t> buffer(required);
    const DWORD written = GetFullPathNameW(
        input, required, buffer.data(), nullptr);
    if (written == 0 || written >= required) {
        return false;
    }
    output.assign(buffer.data(), written);
    return true;
}

std::wstring join_path(const std::wstring &directory, const wchar_t *leaf)
{
    std::wstring result = directory;
    if (!result.empty() && result.back() != L'\\' &&
        result.back() != L'/') {
        result.push_back(L'\\');
    }
    result += leaf;
    return result;
}

bool load_streamline(const std::wstring &interposer,
                     SlExports &out, std::string &error)
{
    if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) {
        error = "sl.interposer.dll failed NVIDIA signature verification";
        return false;
    }

    out.module = LoadLibraryExW(
        interposer.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (out.module == nullptr) {
        error = "LoadLibraryExW(sl.interposer.dll) failed";
        return false;
    }

    out.init = load_export<PFun_slInit *>(out.module, "slInit");
    out.shutdown =
        load_export<PFun_slShutdown *>(out.module, "slShutdown");
    out.get_feature_requirements =
        load_export<PFun_slGetFeatureRequirements *>(
            out.module, "slGetFeatureRequirements");
    out.is_feature_supported =
        load_export<PFun_slIsFeatureSupported *>(
            out.module, "slIsFeatureSupported");
    out.get_new_frame_token =
        load_export<PFun_slGetNewFrameToken *>(
            out.module, "slGetNewFrameToken");
    out.set_tag_for_frame =
        load_export<PFun_slSetTagForFrame *>(
            out.module, "slSetTagForFrame");
    out.evaluate_feature =
        load_export<PFun_slEvaluateFeature *>(
            out.module, "slEvaluateFeature");
    out.get_instance_proc_addr =
        load_export<PFN_vkGetInstanceProcAddr>(
            out.module, "vkGetInstanceProcAddr");
    out.get_device_proc_addr =
        load_export<PFN_vkGetDeviceProcAddr>(
            out.module, "vkGetDeviceProcAddr");

    if (out.init == nullptr || out.shutdown == nullptr ||
        out.get_feature_requirements == nullptr ||
        out.is_feature_supported == nullptr ||
        out.get_new_frame_token == nullptr ||
        out.set_tag_for_frame == nullptr ||
        out.evaluate_feature == nullptr ||
        out.get_instance_proc_addr == nullptr ||
        out.get_device_proc_addr == nullptr) {
        error = "sl.interposer.dll is missing required Streamline exports";
        FreeLibrary(out.module);
        out = {};
        return false;
    }
    return true;
}

XemuNeuralPluginResult bootstrap_adapter(
    const XemuNeuralPluginHostV1 *host, void **context_out,
    XemuNeuralPluginVulkanRequirementsV1 *requirements_out,
    XemuNeuralPluginStatusV1 *status)
{
    if (host == nullptr || context_out == nullptr ||
        requirements_out == nullptr || status == nullptr) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    auto context = new (std::nothrow) BootstrapContext{};
    if (context == nullptr) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    context->host = *host;

    const wchar_t *directory = _wgetenv(L"XEMU_STREAMLINE_DIRECTORY");
    if (directory == nullptr || directory[0] == L'\0') {
        std::snprintf(status->text, sizeof(status->text),
                      "XEMU_STREAMLINE_DIRECTORY is not set");
        delete context;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    if (!make_absolute_path(directory, context->streamline_directory)) {
        std::snprintf(status->text, sizeof(status->text),
                      "Cannot resolve XEMU_STREAMLINE_DIRECTORY");
        delete context;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    context->plugin_paths[0] = context->streamline_directory.c_str();

    const char *project_id = std::getenv("XEMU_STREAMLINE_PROJECT_ID");
    if (!is_guid_text(project_id)) {
        std::snprintf(status->text, sizeof(status->text),
                      "XEMU_STREAMLINE_PROJECT_ID must be a GUID");
        delete context;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    context->project_id = project_id;

    const wchar_t *log_directory =
        _wgetenv(L"XEMU_STREAMLINE_LOG_DIRECTORY");
    if (log_directory != nullptr && log_directory[0] != L'\0') {
        if (!make_absolute_path(log_directory, context->log_directory)) {
            std::snprintf(status->text, sizeof(status->text),
                          "Cannot resolve XEMU_STREAMLINE_LOG_DIRECTORY");
            delete context;
            return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
        }
    }

    std::string error;
    if (!load_streamline(
            join_path(context->streamline_directory, L"sl.interposer.dll"),
            context->sl, error)) {
        std::snprintf(status->text, sizeof(status->text), "%s",
                      error.c_str());
        delete context;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    const sl::Feature features[] = {sl::kFeatureDLSS_NR};
    sl::Preferences preferences{};
    preferences.flags =
        sl::PreferenceFlags::eDisableCLStateTracking |
        sl::PreferenceFlags::eDisableDebugText |
        sl::PreferenceFlags::eUseManualHooking |
        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.pathsToPlugins = context->plugin_paths.data();
    preferences.numPathsToPlugins =
        static_cast<uint32_t>(context->plugin_paths.size());
    preferences.pathToLogsAndData = context->log_directory.empty()
                                        ? nullptr
                                        : context->log_directory.c_str();
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad =
        static_cast<uint32_t>(std::size(features));
    preferences.applicationId = 0;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "xemu-neural-present-0.2";
    preferences.projectId = context->project_id.c_str();
    preferences.renderAPI = sl::RenderAPI::eVulkan;

    if (context->sl.init(preferences, sl::kSDKVersion) != sl::Result::eOk) {
        std::snprintf(status->text, sizeof(status->text),
                      "slInit failed for sl.dlss_nr");
        FreeLibrary(context->sl.module);
        delete context;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    if (context->sl.get_feature_requirements(
            sl::kFeatureDLSS_NR, context->requirements) !=
        sl::Result::eOk) {
        std::snprintf(status->text, sizeof(status->text),
                      "slGetFeatureRequirements(kFeatureDLSS_NR) failed");
        context->sl.shutdown();
        FreeLibrary(context->sl.module);
        delete context;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    context->create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        context->sl.get_instance_proc_addr(nullptr, "vkCreateInstance"));
    if (context->create_instance == nullptr) {
        std::snprintf(status->text, sizeof(status->text),
                      "Streamline did not expose vkCreateInstance proxy");
        context->sl.shutdown();
        FreeLibrary(context->sl.module);
        delete context;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    copy_names(context->instance_extensions,
               context->requirements.vkNumInstanceExtensions,
               context->requirements.vkInstanceExtensions);
    copy_names(context->device_extensions,
               context->requirements.vkNumDeviceExtensions,
               context->requirements.vkDeviceExtensions);
    copy_names(context->feature12_names,
               context->requirements.vkNumFeatures12,
               context->requirements.vkFeatures12);
    copy_names(context->feature13_names,
               context->requirements.vkNumFeatures13,
               context->requirements.vkFeatures13);

    *requirements_out = {};
    requirements_out->struct_size = sizeof(*requirements_out);
    requirements_out->minimum_api_version = VK_API_VERSION_1_3;
    requirements_out->instance_extension_count =
        static_cast<uint32_t>(context->instance_extensions.size());
    requirements_out->instance_extensions =
        context->instance_extensions.data();
    requirements_out->device_extension_count =
        static_cast<uint32_t>(context->device_extensions.size());
    requirements_out->device_extensions =
        context->device_extensions.data();
    requirements_out->feature12_count =
        static_cast<uint32_t>(context->feature12_names.size());
    requirements_out->feature12_names = context->feature12_names.data();
    requirements_out->feature13_count =
        static_cast<uint32_t>(context->feature13_names.size());
    requirements_out->feature13_names = context->feature13_names.data();
    requirements_out->additional_graphics_queue_count =
        context->requirements.vkNumGraphicsQueuesRequired;
    requirements_out->additional_compute_queue_count =
        context->requirements.vkNumComputeQueuesRequired;
    requirements_out->additional_optical_flow_queue_count =
        context->requirements.vkNumOpticalFlowQueuesRequired;

    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
    std::snprintf(status->text, sizeof(status->text),
                  "Streamline initialized; waiting for Vulkan device");
    *context_out = context;
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

void shutdown_adapter(void *opaque)
{
    auto context = static_cast<BootstrapContext *>(opaque);
    if (context == nullptr) {
        return;
    }
    if (context->sl.shutdown != nullptr) {
        context->sl.shutdown();
    }
    if (context->sl.module != nullptr) {
        FreeLibrary(context->sl.module);
    }
    delete context;
}

int32_t create_instance_proxy(
    void *opaque, const void *create_info,
    const void *allocation_callbacks, uintptr_t *instance_out)
{
    auto context = static_cast<BootstrapContext *>(opaque);
    if (context == nullptr || create_info == nullptr ||
        instance_out == nullptr || context->create_instance == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = context->create_instance(
        static_cast<const VkInstanceCreateInfo *>(create_info),
        static_cast<const VkAllocationCallbacks *>(allocation_callbacks),
        &instance);
    if (result == VK_SUCCESS) {
        context->created_instance = instance;
    }
    *instance_out = reinterpret_cast<uintptr_t>(instance);
    return result;
}

int32_t create_device_proxy(
    void *opaque, uintptr_t physical_device,
    const void *create_info, const void *allocation_callbacks,
    uintptr_t *device_out)
{
    auto context = static_cast<BootstrapContext *>(opaque);
    if (context == nullptr || physical_device == 0 || create_info == nullptr ||
        device_out == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (context->create_device == nullptr) {
        /* Resolve only after an instance exists. */
        context->create_device = reinterpret_cast<PFN_vkCreateDevice>(
            context->sl.get_instance_proc_addr(
                context->created_instance, "vkCreateDevice"));
    }
    if (context->create_device == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDevice device = VK_NULL_HANDLE;
    VkResult result = context->create_device(
        reinterpret_cast<VkPhysicalDevice>(physical_device),
        static_cast<const VkDeviceCreateInfo *>(create_info),
        static_cast<const VkAllocationCallbacks *>(allocation_callbacks),
        &device);
    *device_out = reinterpret_cast<uintptr_t>(device);
    return result;
}

XemuNeuralPluginResult create_runtime(
    const XemuNeuralPluginCreateInfoV1 *create_info, void **context_out,
    XemuNeuralPluginStatusV1 *status)
{
    if (create_info == nullptr || context_out == nullptr || status == nullptr ||
        create_info->bootstrap_context == nullptr ||
        create_info->vulkan.device == 0 || create_info->vulkan.queue == 0) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    auto runtime = new (std::nothrow) RuntimeContext{};
    if (runtime == nullptr) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    runtime->bootstrap = static_cast<BootstrapContext *>(
        create_info->bootstrap_context);
    runtime->instance =
        reinterpret_cast<VkInstance>(create_info->vulkan.instance);
    runtime->physical_device = reinterpret_cast<VkPhysicalDevice>(
        create_info->vulkan.physical_device);
    runtime->device =
        reinterpret_cast<VkDevice>(create_info->vulkan.device);
    runtime->queue =
        reinterpret_cast<VkQueue>(create_info->vulkan.queue);
    runtime->queue_family = create_info->vulkan.queue_family_index;

    sl::AdapterInfo adapter_info{};
    adapter_info.vkPhysicalDevice = runtime->physical_device;
    const sl::Result support = runtime->bootstrap->sl.is_feature_supported(
        sl::kFeatureDLSS_NR, adapter_info);
    if (support != sl::Result::eOk) {
        std::snprintf(status->text, sizeof(status->text),
                      "sl.dlss_nr is unsupported on the selected adapter "
                      "(Streamline result %u)",
                      static_cast<unsigned int>(support));
        delete runtime;
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    /*
     * If the create proxies were not used, call slSetVulkanInfo here and pass
     * every actual queue family/index. With the proxy path, Streamline already
     * receives instance/device creation.
     *
     * Allocate private NR output/history images here after querying the exact
     * supported format/extent contract. A transactional adapter must retain
     * the original xemu display image until evaluate completes.
     */

    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
    std::snprintf(status->text, sizeof(status->text),
                  "Streamline reports sl.dlss_nr support; feature contract "
                  "and presentation tick are not armed");
    *context_out = runtime;
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

XemuNeuralPluginResult prepare_frame(
    void *opaque, const XemuNeuralPluginFrameV1 *frame,
    XemuNeuralPluginStatusV1 *status)
{
    auto runtime = static_cast<RuntimeContext *>(opaque);
    if (runtime == nullptr || frame == nullptr || status == nullptr ||
        frame->color_image == 0 || frame->color_image_view == 0) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    /* Recreate private output/history resources on extent/format changes. */
    status->state = XEMU_NEURAL_PLUGIN_STATE_BYPASSED;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
    std::snprintf(status->text, sizeof(status->text),
                  "Public SDK lacks the final DLSS NR feature contract");
    return XEMU_NEURAL_PLUGIN_RESULT_BYPASS;
}

XemuNeuralPluginResult record_frame(
    void *opaque, const XemuNeuralPluginVulkanRecordV1 *record,
    XemuNeuralPluginFrameOutputV1 *output)
{
    auto runtime = static_cast<RuntimeContext *>(opaque);
    if (runtime == nullptr || record == nullptr || output == nullptr ||
        record->command_buffer == 0) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

#if defined(XEMU_HAVE_NVIDIA_DLSS_NR_PARTNER_CONTRACT)
    /*
     * Exact production sequence, to be filled with the matching package:
     *
     * 1. Transition/copy xemu color into an adapter-owned NR input image.
     * 2. Produce or import motion in the contract's documented direction,
     *    units, extent, and format. Add validated depth/control mask only if
     *    required by that runtime.
     * 3. Acquire a frame token:
     *
     *      sl::FrameToken *token = nullptr;
     *      runtime->bootstrap->sl.get_new_frame_token(
     *          token, &runtime->frame_index);
     *
     * 4. Build sl::Resource values with exact Vulkan image, view, format,
     *    usage and current layout. Tag at least the documented uplift input
     *    and output resources using slSetTagForFrame. The public constants are
     *    kBufferTypeUpliftInputColor, kBufferTypeUpliftOutputColor and
     *    kBufferTypeUpliftControlMask.
     * 5. Populate every NR-specific option/constant structure from the partner
     *    header. Do not reuse DLSS-SR/DLAA options unless the contract says so.
     * 6. Satisfy Streamline common-plugin presentation lifecycle. xemu does
     *    not own a Vulkan swapchain, so the normal vkQueuePresentKHR proxy is
     *    never called. Use only an NVIDIA-documented presentation callback or
     *    an adapter-owned presentation bridge. Do not invent or call private
     *    Streamline symbols.
     * 7. Call:
     *
     *      slEvaluateFeature(sl::kFeatureDLSS_NR, *token,
     *                        inputs, input_count,
     *                        reinterpret_cast<sl::CommandBuffer *>(cmd));
     *
     * 8. Barrier and copy the complete private NR output over xemu's display
     *    image, then return it to record->frame.required_return_layout.
     * 9. Only after all calls return success set COLOR_WRITTEN, HISTORY_VALID,
     *    and feature DLSS_NR.
     */
#else
    (void)record;
    std::snprintf(output->text, sizeof(output->text),
                  "DLSS NR partner contract was not compiled into adapter");
    output->feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
    output->flags = 0;
    return XEMU_NEURAL_PLUGIN_RESULT_BYPASS;
#endif
}

void destroy_runtime(void *opaque)
{
    auto runtime = static_cast<RuntimeContext *>(opaque);
    /* Wait/retire private work slots and free Streamline resources first. */
    delete runtime;
}

void reset_history(void *opaque, uint32_t reason)
{
    auto runtime = static_cast<RuntimeContext *>(opaque);
    (void)reason;
    if (runtime != nullptr) {
        runtime->frame_index++;
        /* Mark the next real evaluate as reset and invalidate motion history. */
    }
}

void release_resources(void *opaque)
{
    auto runtime = static_cast<RuntimeContext *>(opaque);
    if (runtime != nullptr) {
        /* Retire and destroy extent-dependent output/history resources. */
    }
}

void get_status(void *opaque, XemuNeuralPluginStatusV1 *status)
{
    if (status == nullptr) {
        return;
    }
    auto runtime = static_cast<RuntimeContext *>(opaque);
    status->state = runtime != nullptr ? XEMU_NEURAL_PLUGIN_STATE_BYPASSED
                                       : XEMU_NEURAL_PLUGIN_STATE_FAILED;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
    std::snprintf(status->text, sizeof(status->text),
                  "Streamline outline; genuine NR contract not installed");
}

const XemuNeuralPluginApiV1 kApi = {
    sizeof(XemuNeuralPluginApiV1),
    XEMU_NEURAL_PLUGIN_ABI_VERSION,
    XEMU_NEURAL_PLUGIN_CAP_VULKAN_RECORD |
        XEMU_NEURAL_PLUGIN_CAP_TRANSACTIONAL_IN_PLACE |
        XEMU_NEURAL_PLUGIN_CAP_INTERNAL_MOTION |
        XEMU_NEURAL_PLUGIN_CAP_DEPTH_OPTIONAL |
        XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY,
    "xemu-streamline-dlss-nr-outline",
    "0.2.0",
    bootstrap_adapter,
    shutdown_adapter,
    create_instance_proxy,
    create_device_proxy,
    create_runtime,
    destroy_runtime,
    prepare_frame,
    record_frame,
    reset_history,
    release_resources,
    get_status,
};

} // namespace

extern "C" XEMU_NEURAL_PLUGIN_EXPORT const XemuNeuralPluginApiV1 *
xemu_neural_present_get_api(uint32_t requested_abi_version)
{
    return requested_abi_version == XEMU_NEURAL_PLUGIN_ABI_VERSION ? &kApi
                                                                   : nullptr;
}
