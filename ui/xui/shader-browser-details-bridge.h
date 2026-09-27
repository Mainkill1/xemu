/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_SHADER_BROWSER_DETAILS_BRIDGE_H
#define XEMU_SHADER_BROWSER_DETAILS_BRIDGE_H

#include "shader-browser-session-provider.hh"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    XEMU_SHADER_BROWSER_DETAIL_OPENGL = 1,
    XEMU_SHADER_BROWSER_DETAIL_VULKAN = 2,
};

enum {
    XEMU_SHADER_BROWSER_DETAIL_COMPLETE = 1,
    XEMU_SHADER_BROWSER_DETAIL_PARTIAL = 2,
    XEMU_SHADER_BROWSER_DETAIL_UNAVAILABLE = 3,
    XEMU_SHADER_BROWSER_DETAIL_FAILED = 4,
};

enum {
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_VERTEX = 1,
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_GEOMETRY = 2,
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_FRAGMENT = 3,
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_LINKED_PROGRAM = 4,
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_PIPELINE = 5,
};

enum {
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_GLSL = 1,
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_SPIRV_TEXT = 2,
    XEMU_SHADER_BROWSER_DETAIL_SOURCE_METADATA = 3,
};

enum {
    XEMU_SHADER_BROWSER_DETAIL_VALID_FRAMES = 1U << 0,
    XEMU_SHADER_BROWSER_DETAIL_VALID_DRAW_COUNT = 1U << 1,
    XEMU_SHADER_BROWSER_DETAIL_VALID_COMPILE_TIME = 1U << 2,
    XEMU_SHADER_BROWSER_DETAIL_VALID_CREATE_TIME = 1U << 3,
    XEMU_SHADER_BROWSER_DETAIL_VALID_PRIMITIVE_MODE = 1U << 4,
    XEMU_SHADER_BROWSER_DETAIL_VALID_COLOR_FORMAT = 1U << 5,
    XEMU_SHADER_BROWSER_DETAIL_VALID_DEPTH_FORMAT = 1U << 6,
    XEMU_SHADER_BROWSER_DETAIL_VALID_VERTEX_LAYOUT = 1U << 7,
};

enum {
    XEMU_SHADER_BROWSER_DETAIL_EVENT_DISCOVERED = 1,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_SOURCE_GENERATED = 2,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_COMPILE_QUEUED = 3,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_COMPILE_STARTED = 4,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_COMPILE_COMPLETED = 5,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_COMPILE_FAILED = 6,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_MODULE_PUBLISHED = 7,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_PIPELINE_REQUESTED = 8,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_PIPELINE_CREATED = 9,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_PIPELINE_PUBLISHED = 10,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_SELECTED = 11,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_REMOVED = 12,
    XEMU_SHADER_BROWSER_DETAIL_EVENT_REQUEST_COMPLETED = 13,
};

typedef struct XemuShaderBrowserDetailRequest {
    uint64_t request_id;
    uint32_t identity_version;
    uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
    uint32_t flags;
} XemuShaderBrowserDetailRequest;

typedef struct XemuShaderBrowserDetailSource {
    uint32_t stage;
    uint32_t kind;
    uint32_t route;
    uint64_t artifact_id;
    int exact_runtime_source;
    const char *label;
    const char *text;
    size_t text_size;
} XemuShaderBrowserDetailSource;

typedef struct XemuShaderBrowserDetailVariant {
    uint64_t variant_id;
    uint32_t route;
    uint32_t readiness;
    uint32_t valid_fields;
    uint64_t first_frame;
    uint64_t last_frame;
    uint64_t draw_count;
    uint64_t compile_time_ns;
    uint64_t create_time_ns;
    uint32_t primitive_mode;
    uint32_t color_format;
    uint32_t depth_format;
    uint32_t vertex_binding_count;
    uint32_t vertex_attribute_count;
    const char *label;
} XemuShaderBrowserDetailVariant;

typedef struct XemuShaderBrowserDetailLifecycle {
    uint64_t sequence;
    uint64_t frame;
    uint32_t kind;
    uint32_t route;
    uint64_t artifact_id;
    const char *message;
} XemuShaderBrowserDetailLifecycle;

typedef struct XemuShaderBrowserDetailResult {
    XemuShaderBrowserDetailRequest request;
    uint32_t backend;
    uint32_t state;
    const char *status;
    const XemuShaderBrowserDetailSource *sources;
    size_t source_count;
    const XemuShaderBrowserDetailVariant *variants;
    size_t variant_count;
    const XemuShaderBrowserDetailLifecycle *lifecycle;
    size_t lifecycle_count;
    uint64_t dropped_lifecycle_events;
} XemuShaderBrowserDetailResult;

/* Cheap owner-loop gate. No renderer work is performed while it is false. */
int xemu_shader_browser_details_pending(void);
int xemu_shader_browser_details_try_claim(
    uint32_t backend, XemuShaderBrowserDetailRequest *request);
int xemu_shader_browser_details_complete(
    const XemuShaderBrowserDetailResult *result);
int xemu_shader_browser_details_abandon(uint64_t request_id,
                                        uint32_t backend,
                                        const char *reason);
void xemu_shader_browser_details_reset(void);

#ifdef __cplusplus
}
#endif
#endif
