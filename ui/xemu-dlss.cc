/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xemu-dlss.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace {
std::mutex lock;
XemuDLSSSnapshot snapshot{};
std::string saved_path, executable_directory;
std::string env_values[4];
bool env_present[4]{};
bool host_is_windows;
uint64_t producer_generation;
bool producer_live;

template <size_t N> void copy_text(char (&dest)[N], const char *source)
{
    std::snprintf(dest, N, "%s", source ? source : "");
}

bool absolute_path(const std::string &path, bool windows)
{
    if (path.empty()) {
        return false;
    }
    if (!windows) {
        return path[0] == '/';
    }
    auto slash = [](char c) { return c == '/' || c == '\\'; };
    bool drive = path.size() >= 3 &&
                 std::isalpha(static_cast<unsigned char>(path[0])) &&
                 path[1] == ':' && slash(path[2]);
    bool unc = path.size() >= 5 && slash(path[0]) && slash(path[1]) &&
               !slash(path[2]);
    return drive || unc;
}

bool parse_switch(const char *text, bool *value)
{
    std::string v = text ? text : "";
    for (char &c : v) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        *value = true;
        return true;
    }
    if (v.empty() || v == "0" || v == "false" || v == "no" || v == "off") {
        *value = false;
        return true;
    }
    return false;
}

bool parse_dimension(const char *text, uint32_t *out)
{
    if (!text || !*text) {
        return false;
    }
    uint32_t value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(*p - '0');
        if (value > 8192) { /* Also prevents integer overflow. */
            return false;
        }
    }
    *out = value;
    return true;
}

void reject(XemuDLSSLaunch *out, const char *reason)
{
    out->valid = false;
    out->adapter_path[0] = '\0';
    copy_text(out->reason, reason);
}

bool same_launch(const XemuDLSSLaunch &a, const XemuDLSSLaunch &b)
{
    if (a.valid != b.valid || a.mode != b.mode) {
        return false;
    }
    if (a.mode == XEMU_DLSS_OFF) {
        return true;
    }
    return a.processing_resolution == b.processing_resolution &&
           a.work_width == b.work_width && a.work_height == b.work_height &&
           std::strcmp(a.adapter_path, b.adapter_path) == 0;
}

void set_requested_locked(const XemuDLSSRequest *request)
{
    saved_path = request->adapter_path ? request->adapter_path : "";
    XemuDLSSRequest owned = *request;
    owned.adapter_path = saved_path.c_str();
    XemuDLSSEnvironment env{
        env_present[0] ? env_values[0].c_str() : nullptr,
        env_present[1] ? env_values[1].c_str() : nullptr,
        env_present[2] ? env_values[2].c_str() : nullptr,
        env_present[3] ? env_values[3].c_str() : nullptr,
    };
    xemu_dlss_resolve(&owned, &env, executable_directory.c_str(),
                      host_is_windows, &snapshot.next_launch);
    snapshot.requested_mode = request->mode;
    snapshot.restart_required = snapshot.initialized &&
        !same_launch(snapshot.startup, snapshot.next_launch);
    ++snapshot.sequence;
}
} // namespace

void xemu_dlss_resolve(const XemuDLSSRequest *request,
                       const XemuDLSSEnvironment *environment,
                       const char *directory, bool windows,
                       XemuDLSSLaunch *out)
{
    if (!out) {
        return;
    }
    *out = {};
    if (!request) {
        reject(out, "DLSS settings have not been initialized.");
        return;
    }
    XemuDLSSEnvironment empty{};
    const auto &env = environment ? *environment : empty;
    out->mode = request->mode;
    out->processing_resolution = request->processing_resolution;
    if (request->mode < XEMU_DLSS_OFF || request->mode > XEMU_DLSS_NEURAL_RENDERING ||
        request->adapter_selection < XEMU_DLSS_ADAPTER_AUTO ||
        request->adapter_selection > XEMU_DLSS_ADAPTER_CUSTOM ||
        request->processing_resolution < XEMU_DLSS_RESOLUTION_AUTO ||
        request->processing_resolution > XEMU_DLSS_RESOLUTION_DISPLAY) {
        out->mode = XEMU_DLSS_OFF;
        reject(out, "Invalid DLSS setting. Select a valid mode, adapter and resolution.");
        return;
    }
    if (env.enable) {
        out->environment_overrides |= XEMU_DLSS_OVERRIDE_ENABLE;
        bool enabled = false;
        if (!parse_switch(env.enable, &enabled)) {
            out->mode = XEMU_DLSS_OFF;
            reject(out, "Invalid XEMU_EXPERIMENTAL_NEURAL_PRESENT Boolean.");
            return;
        }
        out->mode = enabled ? (request->mode == XEMU_DLSS_OFF ?
                               XEMU_DLSS_NEURAL_RENDERING : request->mode) :
                              XEMU_DLSS_OFF;
    }
    if (env.plugin) {
        out->environment_overrides |= XEMU_DLSS_OVERRIDE_PATH;
    }
    if (env.width || env.height) {
        out->environment_overrides |= XEMU_DLSS_OVERRIDE_EXTENT;
        if (!parse_dimension(env.width, &out->work_width) ||
            !parse_dimension(env.height, &out->work_height) ||
            ((out->work_width == 0) != (out->work_height == 0))) {
            reject(out, "Set both work dimensions to 1..8192, or both to 0 for display size.");
            return;
        }
        /* Exact developer override; zero/zero explicitly means display size. */
        out->processing_resolution = XEMU_DLSS_RESOLUTION_DISPLAY;
    }
    if (out->mode == XEMU_DLSS_OFF) {
        out->valid = true;
        return;
    }
    if (!windows) {
        reject(out, "External DLSS adapters currently require a Windows build.");
        return;
    }
    std::string path;
    if (env.plugin) {
        path = env.plugin;
    } else if (request->adapter_selection == XEMU_DLSS_ADAPTER_CUSTOM) {
        path = request->adapter_path ? request->adapter_path : "";
    } else {
        path = directory ? directory : "";
        if (!absolute_path(path, windows)) {
            reject(out, "Unable to locate xemu's executable directory. Choose a custom adapter.");
            return;
        }
        if (path.back() != '/' && path.back() != '\\') {
            path += '/';
        }
        path += "xemu-neural-present.dll";
    }
    if (!absolute_path(path, windows)) {
        reject(out, "Choose an absolute adapter DLL path. Relative or empty paths are not loaded.");
        return;
    }
    if (path.size() >= sizeof(out->adapter_path)) {
        reject(out, "Adapter path exceeds the 4095-byte limit; no truncated path will be loaded.");
        return;
    }
    copy_text(out->adapter_path, path.c_str());
    out->valid = true;
}

bool xemu_dlss_adapter_allowed(int mode, uint64_t capabilities)
{
    return mode == XEMU_DLSS_NEURAL_RENDERING ||
        (mode == XEMU_DLSS_VALIDATION &&
         (capabilities & XEMU_NEURAL_PLUGIN_CAP_VALIDATION_ONLY));
}

void xemu_dlss_processing_extent(const XemuDLSSLaunch *launch,
                                 uint32_t source_width, uint32_t source_height,
                                 uint32_t *width, uint32_t *height)
{
    if (!width || !height) {
        return;
    }
    *width = *height = 0;
    if (!launch || !source_width || !source_height) {
        return;
    }
    if (launch->work_width && launch->work_height) {
        *width = launch->work_width;
        *height = launch->work_height;
        return;
    }
    *width = source_width;
    *height = source_height;
    if (launch->processing_resolution == XEMU_DLSS_RESOLUTION_DISPLAY) {
        return;
    }
    uint32_t max_w = launch->processing_resolution == XEMU_DLSS_RESOLUTION_1080 ? 1920 : 1280;
    uint32_t max_h = launch->processing_resolution == XEMU_DLSS_RESOLUTION_1080 ? 1080 : 720;
    if (source_width <= max_w && source_height <= max_h) {
        return;
    }
    if (uint64_t(source_width) * max_h > uint64_t(source_height) * max_w) {
        *width = max_w;
        *height = std::max(uint32_t(1), uint32_t(uint64_t(source_height) * max_w / source_width));
    } else {
        *height = max_h;
        *width = std::max(uint32_t(1), uint32_t(uint64_t(source_width) * max_h / source_height));
    }
}

bool xemu_dlss_initialize(const XemuDLSSRequest *request,
                          const XemuDLSSEnvironment *environment,
                          const char *directory, bool windows)
{
    if (!request) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock);
    if (snapshot.initialized) {
        return false;
    }
    executable_directory = directory ? directory : "";
    host_is_windows = windows;
    XemuDLSSEnvironment empty{};
    const auto &env = environment ? *environment : empty;
    const char *values[] = {env.enable, env.plugin, env.width, env.height};
    for (unsigned i = 0; i < 4; ++i) {
        env_present[i] = values[i] != nullptr;
        env_values[i] = values[i] ? values[i] : "";
    }
    set_requested_locked(request);
    snapshot.startup = snapshot.next_launch;
    snapshot.initialized = true;
    snapshot.runtime.state = snapshot.startup.mode == XEMU_DLSS_OFF ?
        XEMU_DLSS_RUNTIME_DISABLED : XEMU_DLSS_RUNTIME_NOT_STARTED;
    if (!snapshot.startup.valid) {
        snapshot.runtime.state = XEMU_DLSS_RUNTIME_UNAVAILABLE;
        copy_text(snapshot.runtime.message, snapshot.startup.reason);
    }
    return true;
}

void xemu_dlss_set_requested(const XemuDLSSRequest *request)
{
    if (!request) {
        return;
    }
    std::lock_guard<std::mutex> guard(lock);
    set_requested_locked(request);
}

void xemu_dlss_get_launch(XemuDLSSLaunch *out)
{
    if (out) {
        std::lock_guard<std::mutex> guard(lock);
        *out = snapshot.startup;
    }
}

void xemu_dlss_get_snapshot(XemuDLSSSnapshot *out)
{
    if (out) {
        std::lock_guard<std::mutex> guard(lock);
        *out = snapshot;
    }
}

uint64_t xemu_dlss_begin_runtime(void)
{
    std::lock_guard<std::mutex> guard(lock);
    if (++producer_generation == 0) {
        ++producer_generation;
    }
    producer_live = true;
    snapshot.runtime = {};
    snapshot.runtime.state = XEMU_DLSS_RUNTIME_LOADING;
    ++snapshot.sequence;
    return producer_generation;
}

void xemu_dlss_publish_runtime(uint64_t token, const XemuDLSSRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    std::lock_guard<std::mutex> guard(lock);
    if (!producer_live || token != producer_generation) {
        return;
    }
    snapshot.runtime = *runtime;
    auto &r = snapshot.runtime;
    r.adapter_name[sizeof(r.adapter_name) - 1] = '\0';
    r.adapter_version[sizeof(r.adapter_version) - 1] = '\0';
    r.gpu_name[sizeof(r.gpu_name) - 1] = '\0';
    r.message[sizeof(r.message) - 1] = '\0';
    if (r.state != XEMU_DLSS_RUNTIME_ACTIVE ||
        r.feature > XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR ||
        !r.processed_frames ||
        (r.feature == XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR && !r.nr_frames)) {
        r.feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
        r.gpu_time_ns = 0;
    }
    ++snapshot.sequence;
}

void xemu_dlss_end_runtime(uint64_t token)
{
    std::lock_guard<std::mutex> guard(lock);
    if (!producer_live || token != producer_generation) {
        return;
    }
    producer_live = false;
    snapshot.runtime = {};
    snapshot.runtime.state = XEMU_DLSS_RUNTIME_NOT_STARTED;
    copy_text(snapshot.runtime.message, "Vulkan renderer is not active.");
    ++snapshot.sequence;
}

const char *xemu_dlss_mode_name(int mode)
{
    switch (mode) {
    case XEMU_DLSS_OFF: return "Off";
    case XEMU_DLSS_VALIDATION: return "Adapter Validation";
    case XEMU_DLSS_NEURAL_RENDERING: return "DLSS Neural Rendering - Experimental";
    default: return "Invalid";
    }
}

const char *xemu_dlss_runtime_state_name(XemuDLSSRuntimeState state)
{
    switch (state) {
    case XEMU_DLSS_RUNTIME_NOT_STARTED: return "Not started";
    case XEMU_DLSS_RUNTIME_DISABLED: return "Disabled";
    case XEMU_DLSS_RUNTIME_LOADING: return "Loading";
    case XEMU_DLSS_RUNTIME_MISSING_ADAPTER: return "Adapter missing or could not load";
    case XEMU_DLSS_RUNTIME_READY: return "Ready - no produced frame yet";
    case XEMU_DLSS_RUNTIME_ACTIVE: return "Processing frames";
    case XEMU_DLSS_RUNTIME_BYPASSED: return "Bypassed - original output";
    case XEMU_DLSS_RUNTIME_FAILED: return "Failed";
    case XEMU_DLSS_RUNTIME_UNAVAILABLE: return "Unavailable";
    }
    return "Unknown";
}
