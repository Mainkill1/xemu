/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_SHADER_BROWSER_DRAW_INPUTS_H
#define XEMU_SHADER_BROWSER_DRAW_INPUTS_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All pixels are tightly packed, top-down RGBA8. The bridge copies immediately.
 */
typedef struct XemuShaderDrawImage {
    uint32_t width, height;
    const uint8_t *rgba;
    size_t byte_count;
} XemuShaderDrawImage;

typedef struct XemuShaderDrawTexture {
    uint32_t slot;
    int bound;
    uint32_t guest_format, host_format;
    uint32_t width, height, depth, mip_levels, face_count;
    uint32_t min_filter, mag_filter, wrap_s, wrap_t, wrap_r;
    float coordinate_scale;
    uint32_t mip_level, face;
    XemuShaderDrawImage image;
} XemuShaderDrawTexture;

enum XemuShaderDrawUniformType {
    XEMU_SHADER_DRAW_UNIFORM_FLOAT = 1,
    XEMU_SHADER_DRAW_UNIFORM_INT,
    XEMU_SHADER_DRAW_UNIFORM_UINT,
    XEMU_SHADER_DRAW_UNIFORM_MAT2,
    XEMU_SHADER_DRAW_UNIFORM_MAT4,
};
typedef struct XemuShaderDrawUniform {
    uint32_t stage;
    const char *name;
    uint32_t type, components, count;
    const void *data;
    size_t byte_count;
} XemuShaderDrawUniform;

/* Raw evidence is separate from safe preview decoding. Bytes include raw float
 * bits and are never sanitized by the recorder. */
typedef struct XemuShaderDrawBlob {
    const char *name;
    const void *data;
    size_t byte_count;
    uint32_t slot, format, components, stride, count;
    uint64_t offset;
    uint32_t normalized, integer;
} XemuShaderDrawBlob;

int xemu_shader_draw_request_stage_blob(uint64_t token,
                                        const XemuShaderDrawBlob *blob);
enum XemuShaderCaptureEventKind {
    XEMU_SHADER_CAPTURE_DRAW,
    XEMU_SHADER_CAPTURE_CLEAR,
    XEMU_SHADER_CAPTURE_COPY,
    XEMU_SHADER_CAPTURE_UPLOAD,
    XEMU_SHADER_CAPTURE_RESOLVE,
    XEMU_SHADER_CAPTURE_FRAME,
    XEMU_SHADER_CAPTURE_STATE_WRITE,
    XEMU_SHADER_CAPTURE_ALLOCATION,
    XEMU_SHADER_CAPTURE_RESET,
    XEMU_SHADER_CAPTURE_TITLE,
    XEMU_SHADER_CAPTURE_SAVE_STATE,
};
uint64_t xemu_shader_capture_session_begin_event(uint32_t kind, uint64_t frame,
                                                 uint32_t draw,
                                                 uint64_t submission,
                                                 uint64_t scope_generation,
                                                 uint64_t renderer_epoch);
int xemu_shader_capture_session_active(void);
int xemu_shader_capture_session_token(uint64_t token);
int xemu_shader_capture_session_snapshots(uint64_t token);
int xemu_shader_capture_session_reserve(uint64_t token, size_t bytes);
void xemu_shader_capture_session_frame(uint64_t frame,
                                       uint64_t scope_generation,
                                       uint64_t renderer_epoch);
void xemu_shader_capture_session_invalidate(uint64_t scope_generation,
                                            uint64_t renderer_epoch);
void xemu_shader_capture_session_fail_budget(uint64_t token,
                                             const char *reason);
void xemu_shader_capture_session_fail(uint64_t token, const char *reason);

enum XemuShaderCaptureCommandPhase {
    XEMU_SHADER_CAPTURE_HOST_PREPARATION,
    XEMU_SHADER_CAPTURE_AUXILIARY,
    XEMU_SHADER_CAPTURE_MAIN,
};
enum XemuShaderCaptureBatchOutcome {
    XEMU_SHADER_CAPTURE_BATCH_NONE,
    XEMU_SHADER_CAPTURE_BATCH_PENDING,
    XEMU_SHADER_CAPTURE_BATCH_SUBMITTED,
    XEMU_SHADER_CAPTURE_BATCH_SUBMISSION_FAILED,
    XEMU_SHADER_CAPTURE_BATCH_ABORTED,
    XEMU_SHADER_CAPTURE_BATCH_DETACHED,
};
enum XemuShaderCaptureCommandKind {
    XEMU_SHADER_CAPTURE_COMMAND_UNKNOWN,
    XEMU_SHADER_CAPTURE_COMMAND_CPU_UPLOAD,
    XEMU_SHADER_CAPTURE_COMMAND_BUFFER_COPY,
    XEMU_SHADER_CAPTURE_COMMAND_DRAW,
    XEMU_SHADER_CAPTURE_COMMAND_CLEAR,
    XEMU_SHADER_CAPTURE_COMMAND_IMAGE_COPY,
    XEMU_SHADER_CAPTURE_COMMAND_RESOLVE,
    XEMU_SHADER_CAPTURE_COMMAND_CHECKPOINT,
};
uint64_t xemu_shader_capture_session_batch_begin(uint64_t scope_generation,
                                                 uint64_t renderer_epoch);
int xemu_shader_capture_session_batch_current(uint64_t batch);
int xemu_shader_capture_session_batch_hold(uint64_t batch, uint64_t token);
int xemu_shader_capture_session_batch_record(uint64_t batch, uint64_t token,
                                             uint32_t phase, uint64_t ordinal);
int xemu_shader_capture_session_batch_submit(uint64_t batch, int accepted,
                                             uint64_t queue_ordinal,
                                             int32_t backend_result);
int xemu_shader_capture_session_batch_retire(uint64_t batch, int completed,
                                             int32_t backend_result);
int xemu_shader_capture_session_batch_abort(uint64_t batch, uint32_t outcome,
                                            int32_t backend_result);
int xemu_shader_capture_session_describe_command(uint64_t token, uint32_t kind,
                                                 uint64_t source_offset,
                                                 uint64_t destination_offset,
                                                 uint64_t bytes);

int xemu_shader_draw_request_wants_inputs(uint64_t token);
int xemu_shader_draw_request_has_geometry(uint64_t token);
int xemu_shader_draw_request_stage_image(uint64_t token, int before,
                                         const XemuShaderDrawImage *image);
int xemu_shader_draw_request_stage_texture(
    uint64_t token, const XemuShaderDrawTexture *texture);
int xemu_shader_draw_request_stage_uniform(
    uint64_t token, const XemuShaderDrawUniform *uniform);
int xemu_shader_draw_request_stage_source(uint64_t token, uint32_t stage,
                                          const char *source, size_t bytes);
int xemu_shader_draw_request_stage_register(uint64_t token, const char *name,
                                            uint32_t value);
/* Called when owned readback copies are available. Finish also requires proof
 * that this attempt's actual draw command was emitted. */
int xemu_shader_draw_request_inputs_complete(uint64_t token);

/* Owner IDs are assigned on actual backing creation, follow ownership moves,
 * and are retired with that backing. They are process-local lookup tokens;
 * the session emits independent allocation/view IDs. Neither addresses nor
 * content hashes establish identity. Staging copies this descriptor. */
enum XemuShaderCaptureResourceAccess {
    XEMU_SHADER_CAPTURE_RESOURCE_READ,
    XEMU_SHADER_CAPTURE_RESOURCE_PARTIAL_WRITE,
    XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE,
    XEMU_SHADER_CAPTURE_RESOURCE_UNCERTAIN_WRITE,
    XEMU_SHADER_CAPTURE_RESOURCE_RELEASE,
};
enum XemuShaderCaptureResourceKind {
    XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE = 1,
    XEMU_SHADER_CAPTURE_RESOURCE_COLOR,
    XEMU_SHADER_CAPTURE_RESOURCE_DEPTH_STENCIL,
    XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
};
enum XemuShaderCaptureResourceFlags {
    /* No physical byte extent is observable (GL image). Range [0,1) names
     * the opaque whole image, and writes must be UNCERTAIN_WRITE. */
    XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT = 1,
};
typedef struct XemuShaderCaptureResource {
    uint64_t owner, byte_size, offset, size;
    uint32_t access, kind, slot, flags;
} XemuShaderCaptureResource;
uint64_t xemu_shader_capture_resource_new_owner(void);
int xemu_shader_capture_session_resource(
    uint64_t token, const XemuShaderCaptureResource *resource);
/* Associates an existing owned raw blob with this resource extent. */
int xemu_shader_capture_session_resource_snapshot(
    uint64_t token, const XemuShaderCaptureResource *resource,
    const char *blob_name);
void xemu_shader_capture_session_resource_release(uint64_t owner,
                                                  uint64_t byte_size,
                                                  uint32_t kind,
                                                  uint32_t flags);
/* Attaches a retiring owned readback without changing version/order. */
int xemu_shader_capture_session_resource_read_snapshot(uint64_t token,
                                                       uint32_t kind,
                                                       uint32_t slot,
                                                       const char *blob_name);

#ifdef __cplusplus
}
#endif
#endif
