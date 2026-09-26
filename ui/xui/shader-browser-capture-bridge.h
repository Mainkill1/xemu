// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XEMU_SHADER_CAPTURE_HASH_BYTES 12
#define XEMU_SHADER_CAPTURE_SCOPE_BYTES 32
#define XEMU_SHADER_CAPTURE_MAX_ATTRIBUTES 16
#define XEMU_SHADER_CAPTURE_MAX_VERTICES 4096
#define XEMU_SHADER_CAPTURE_MAX_SOURCE (4U * 1024U * 1024U)

typedef struct XemuShaderCaptureIdentity {
    uint32_t stage;
    uint8_t hash[XEMU_SHADER_CAPTURE_HASH_BYTES];
} XemuShaderCaptureIdentity;

typedef struct XemuShaderCaptureRequest {
    uint32_t title_id;
    uint32_t fingerprint_version;
    uint8_t fingerprint[XEMU_SHADER_CAPTURE_SCOPE_BYTES];
    XemuShaderCaptureIdentity selected;
    uint32_t backend;
    uint64_t session_epoch;
    uint64_t renderer_epoch;
    uint64_t scope_generation;
    uint64_t request_nonce;
    uint64_t deadline_ns;
} XemuShaderCaptureRequest;

typedef struct XemuShaderCaptureDraw {
    XemuShaderCaptureRequest context;
    XemuShaderCaptureIdentity stages[3];
    uint32_t stage_count;
    uint32_t primitive;
    uint32_t vertex_count;
    uint16_t attribute_mask;
    const float *attributes[XEMU_SHADER_CAPTURE_MAX_ATTRIBUTES];
    float constant_attributes[XEMU_SHADER_CAPTURE_MAX_ATTRIBUTES][4];
    const void *shader_state;
    size_t shader_state_size;
    const void *vertex_uniforms;
    size_t vertex_uniforms_size;
    const void *pixel_uniforms;
    size_t pixel_uniforms_size;
    const char *vertex_source;
    const char *geometry_source;
    const char *pixel_source;
    uint32_t control_0;
    uint32_t control_1;
    uint32_t blend;
    uint32_t blend_color;
    uint32_t setup_raster;
    uint32_t color_format;
    uint32_t width;
    uint32_t height;
    uint32_t viewport_width;
    uint32_t viewport_height;
    uint32_t scissor_x;
    uint32_t scissor_y;
    uint32_t scissor_width;
    uint32_t scissor_height;
    uint32_t texture_mask;
    uint32_t route;
    uint64_t sampled_nonce;
    uint64_t capture_started_ns;
} XemuShaderCaptureDraw;

typedef struct XemuShaderCaptureAbiSizes {
    size_t shader_state;
    size_t vertex_uniforms;
    size_t pixel_uniforms;
} XemuShaderCaptureAbiSizes;

XemuShaderCaptureAbiSizes xemu_shader_capture_abi_sizes(void);
int xemu_shader_capture_ordinary_2d_sampler(const void *shader_state,
                                             size_t size, uint32_t stage);
int xemu_shader_capture_pixel_scale_is(const void *pixel_uniforms,
                                        size_t size, uint32_t stage,
                                        float expected);
#ifdef XEMU_SHADER_CAPTURE_TESTING
void xemu_shader_capture_test_make_state(void *bytes, size_t size, int cube);
void xemu_shader_capture_test_make_pixel_uniforms(void *bytes, size_t size);
#endif

// UI arms a single exact selection. The renderer only calls the draw hook
// after a command was actually submitted. A rejected draw never calls it.
void xemu_shader_capture_request(const XemuShaderCaptureRequest *request);
void xemu_shader_capture_cancel(void);
int xemu_shader_capture_armed(void);
uint64_t xemu_shader_capture_nonce(void);
int xemu_shader_capture_copy_request(XemuShaderCaptureRequest *request);
int xemu_shader_capture_submitted(const XemuShaderCaptureDraw *draw,
                                  uint64_t now_ns);
char *xemu_shader_capture_generate_gl_source(const void *shader_state,
                                             size_t shader_state_size,
                                             uint32_t stage);

#ifdef __cplusplus
}
#endif
