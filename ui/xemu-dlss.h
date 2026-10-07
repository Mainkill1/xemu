/*
 * Owned startup configuration and low-frequency DLSS status publication.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef XEMU_DLSS_H
#define XEMU_DLSS_H

#include <stdbool.h>
#include <stdint.h>
#include "hw/xbox/nv2a/pgraph/neural-present-plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XEMU_DLSS_PATH_SIZE 4096
#define XEMU_DLSS_MESSAGE_SIZE 256

enum { XEMU_DLSS_OFF, XEMU_DLSS_VALIDATION, XEMU_DLSS_NEURAL_RENDERING };
enum { XEMU_DLSS_ADAPTER_AUTO, XEMU_DLSS_ADAPTER_CUSTOM };
enum {
    XEMU_DLSS_RESOLUTION_AUTO,
    XEMU_DLSS_RESOLUTION_720,
    XEMU_DLSS_RESOLUTION_1080,
    XEMU_DLSS_RESOLUTION_DISPLAY,
};
enum {
    XEMU_DLSS_OVERRIDE_ENABLE = 1,
    XEMU_DLSS_OVERRIDE_PATH = 2,
    XEMU_DLSS_OVERRIDE_EXTENT = 4,
};
typedef enum XemuDLSSRuntimeState {
    XEMU_DLSS_RUNTIME_NOT_STARTED,
    XEMU_DLSS_RUNTIME_DISABLED,
    XEMU_DLSS_RUNTIME_LOADING,
    XEMU_DLSS_RUNTIME_MISSING_ADAPTER,
    XEMU_DLSS_RUNTIME_READY,
    XEMU_DLSS_RUNTIME_ACTIVE,
    XEMU_DLSS_RUNTIME_BYPASSED,
    XEMU_DLSS_RUNTIME_FAILED,
    XEMU_DLSS_RUNTIME_UNAVAILABLE,
} XemuDLSSRuntimeState;

/* Input pointers are borrowed only for the duration of a call. */
typedef struct XemuDLSSRequest {
    int mode;
    int adapter_selection;
    int processing_resolution;
    const char *adapter_path;
} XemuDLSSRequest;

typedef struct XemuDLSSEnvironment {
    const char *enable;
    const char *plugin;
    const char *width;
    const char *height;
} XemuDLSSEnvironment;

typedef struct XemuDLSSLaunch {
    bool valid;
    int mode;
    int processing_resolution;
    uint32_t environment_overrides;
    uint32_t work_width;
    uint32_t work_height;
    char adapter_path[XEMU_DLSS_PATH_SIZE];
    char reason[XEMU_DLSS_MESSAGE_SIZE];
} XemuDLSSLaunch;

/* Renderer-owned facts, never inferred from the saved request or DLL load. */
typedef struct XemuDLSSRuntime {
    XemuDLSSRuntimeState state;
    XemuNeuralPluginFeature feature;
    uint32_t processing_width;
    uint32_t processing_height;
    uint64_t processed_frames;
    uint64_t bypassed_frames;
    uint64_t failed_frames;
    uint64_t nr_frames;
    uint64_t gpu_time_ns; /* 0 means not supplied, not zero execution cost. */
    char adapter_name[96];
    char adapter_version[64];
    char gpu_name[256];
    char message[XEMU_DLSS_MESSAGE_SIZE];
} XemuDLSSRuntime;

typedef struct XemuDLSSSnapshot {
    bool initialized;
    bool restart_required;
    int requested_mode;
    uint64_t sequence;
    XemuDLSSLaunch startup;
    XemuDLSSLaunch next_launch;
    XemuDLSSRuntime runtime;
} XemuDLSSSnapshot;

/* Pure resolution: no environment reads, file I/O, DLL loading or GPU calls. */
void xemu_dlss_resolve(const XemuDLSSRequest *request,
                       const XemuDLSSEnvironment *environment,
                       const char *executable_directory, bool windows_host,
                       XemuDLSSLaunch *out);
bool xemu_dlss_adapter_allowed(int mode, uint64_t capabilities);
void xemu_dlss_processing_extent(const XemuDLSSLaunch *launch,
                                 uint32_t source_width, uint32_t source_height,
                                 uint32_t *width, uint32_t *height);

/* Call once after settings load, before renderer/workers. Owns all inputs.
 * Later calls return false without changing the startup snapshot. */
bool xemu_dlss_initialize(const XemuDLSSRequest *request,
                          const XemuDLSSEnvironment *environment,
                          const char *executable_directory, bool windows_host);
/* UI thread: publish saved edits only. Never changes the running adapter. */
void xemu_dlss_set_requested(const XemuDLSSRequest *request);
void xemu_dlss_get_launch(XemuDLSSLaunch *out);
void xemu_dlss_get_snapshot(XemuDLSSSnapshot *out);

/* Producer generation prevents stale renderer teardown/publication. Call
 * publish at lifecycle boundaries and at most four times/second in gameplay.
 * No UI callback may enter the adapter or use a renderer-owned pointer. */
uint64_t xemu_dlss_begin_runtime(void);
void xemu_dlss_publish_runtime(uint64_t token, const XemuDLSSRuntime *runtime);
void xemu_dlss_end_runtime(uint64_t token);
const char *xemu_dlss_mode_name(int mode);
const char *xemu_dlss_runtime_state_name(XemuDLSSRuntimeState state);

#ifdef __cplusplus
}
#endif
#endif
