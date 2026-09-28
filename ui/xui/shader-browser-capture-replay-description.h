/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_SHADER_BROWSER_CAPTURE_REPLAY_DESCRIPTION_H
#define XEMU_SHADER_BROWSER_CAPTURE_REPLAY_DESCRIPTION_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum XemuShaderCaptureReplayRole {
    XEMU_SHADER_CAPTURE_REPLAY_TEXTURE,
    XEMU_SHADER_CAPTURE_REPLAY_COLOR,
    XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM,
    XEMU_SHADER_CAPTURE_REPLAY_INDICES,
    XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE,
    XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION,
};
enum XemuShaderCaptureReplayFormat {
    XEMU_SHADER_CAPTURE_REPLAY_FORMAT_UNKNOWN,
    XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM,
    XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM,
};
enum XemuShaderCaptureReplayOrigin {
    XEMU_SHADER_CAPTURE_REPLAY_TOP_DOWN,
    XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP,
};
/* Ordinal counts only matching finalized accesses with nonempty reads/writes.
 * Selectors resolve capture-local versions, never addresses or content hashes.
 */
typedef struct XemuShaderCaptureReplaySelector {
    uint32_t kind, slot, write, ordinal;
} XemuShaderCaptureReplaySelector;
typedef struct XemuShaderCaptureReplayImage {
    uint32_t width, height, mip_level, layer, mip_levels, layers, samples;
    uint32_t format, coordinate_origin;
    /* Component indexes 0..3. Sampling additionally permits 4=zero, 5=one.
     * Capture readbacks and preview results are always canonical top-down RGBA.
     */
    uint32_t storage_to_rgba[4], sample_swizzle[4];
} XemuShaderCaptureReplayImage;
typedef struct XemuShaderCaptureReplayBinding {
    XemuShaderCaptureReplaySelector resource;
    uint32_t role, slot, checkpoint;
    char blob_name[64];
    XemuShaderCaptureReplayImage image;
} XemuShaderCaptureReplayBinding;
#define XEMU_SHADER_CAPTURE_REPLAY_MAX_BINDINGS 32
typedef struct XemuShaderCaptureReplayDescription {
    uint32_t kind, binding_count;
    XemuShaderCaptureReplayBinding
        bindings[XEMU_SHADER_CAPTURE_REPLAY_MAX_BINDINGS];
    uint32_t source_x, source_y, destination_x, destination_y, width, height;
    uint64_t source_offset, destination_offset, bytes;
    /* Actual attachment clear float32 RGBA bits, independent of storage order.
     * Logical color replay supports only mask 0xF, attachment zero, one sample.
     * destination_x/y and width/height are the final native clear rectangle.
     */
    uint32_t clear_color_bits[4], clear_color_mask;
} XemuShaderCaptureReplayDescription;
/* Stage before Finish. Copies metadata only after an armed token check. An
 * image-copy description belongs to the actual recorded copy's batch token.
 * Checkpoints refer only to existing owned inputs of this occurrence. */
int xemu_shader_capture_session_describe_replay(
    uint64_t token, const XemuShaderCaptureReplayDescription *description);
#ifdef __cplusplus
}
#endif
#endif
