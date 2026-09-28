/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_RESOURCE_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_RESOURCE_H

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "hw/xbox/nv2a/debug.h"
#include "ui/xui/shader-browser-draw-request.h"

static inline void pgraph_shader_resource_stage(uint64_t token, uint64_t owner,
                                                uint64_t bytes, uint32_t access,
                                                uint32_t kind, uint32_t slot,
                                                bool opaque)
{
    if (!xemu_shader_capture_session_token(token))
        return;
    XemuShaderCaptureResource resource = {
        .owner = owner,
        .byte_size = bytes,
        .size = bytes,
        .access = access,
        .kind = kind,
        .slot = slot,
        .flags = opaque ? XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT : 0,
    };
    xemu_shader_capture_session_resource(token, &resource);
}

/* Call only at an actual backend owner/command boundary. These metadata
 * occurrences do not increment the guest draw or backend draw submission. */
static inline uint64_t pgraph_shader_resource_begin(PGRAPHState *pg,
                                                    uint32_t kind)
{
    if (!xemu_shader_capture_session_active())
        return 0;
    return xemu_shader_capture_session_begin_event(
        kind, pg->frame_time, pg->draw_time, 0,
        xemu_shader_browser_scope_generation(),
        nv2a_profile_preview_renderer_epoch());
}

static inline void pgraph_shader_resource_finish(uint64_t token)
{
    if (token) {
        xemu_shader_draw_request_finish(token, 0, 0, 0, 0);
        xemu_shader_draw_request_inputs_complete(token);
    }
}

static inline void pgraph_shader_resource_stage_buffer_upload(
    uint64_t token, uint64_t owner, uint64_t bytes, uint64_t offset,
    size_t size, const void *data, bool unknown_source)
{
    if (!token)
        return;
    XemuShaderDrawBlob blob = {
        .name = "resource.buffer.upload",
        .data = data,
        .byte_count = size,
        .offset = offset,
    };
    xemu_shader_draw_request_stage_blob(token, &blob);
    if (unknown_source) {
        // Guest memory can contain a prior GPU download whose producer is
        // unobservable across the host memory boundary.
        XemuShaderCaptureResource source = {
            .size = size,
            .access = XEMU_SHADER_CAPTURE_RESOURCE_READ,
            .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
        };
        xemu_shader_capture_session_resource(token, &source);
    }
    XemuShaderCaptureResource resource = {
        .owner = owner,
        .byte_size = bytes,
        .offset = offset,
        .size = size,
        .access = XEMU_SHADER_CAPTURE_RESOURCE_PARTIAL_WRITE,
        .kind = XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
    };
    xemu_shader_capture_session_resource_snapshot(token, &resource, blob.name);
}

static inline void pgraph_shader_resource_buffer_upload(
    PGRAPHState *pg, uint64_t owner, uint64_t bytes, uint64_t offset,
    size_t size, const void *data, bool unknown_source)
{
    uint64_t token =
        pgraph_shader_resource_begin(pg, XEMU_SHADER_CAPTURE_UPLOAD);
    pgraph_shader_resource_stage_buffer_upload(token, owner, bytes, offset,
                                               size, data, unknown_source);
    pgraph_shader_resource_finish(token);
}

static inline void pgraph_shader_resource_stage_write(
    uint64_t token, uint64_t owner, uint64_t bytes, uint32_t resource_kind,
    bool opaque, uint64_t source_owner, uint64_t source_bytes,
    uint32_t source_kind, bool source_opaque)
{
    if (!token)
        return;
    if (source_owner)
        pgraph_shader_resource_stage(token, source_owner, source_bytes,
                                     XEMU_SHADER_CAPTURE_RESOURCE_READ,
                                     source_kind, 0, source_opaque);
    // Image commands expose logical subresources, not exact tiled memory
    // coverage. Preserve possible prior target content and mark the write gap.
    pgraph_shader_resource_stage(token, owner, bytes,
                                 XEMU_SHADER_CAPTURE_RESOURCE_READ,
                                 resource_kind, 0, opaque);
    pgraph_shader_resource_stage(token, owner, bytes,
                                 XEMU_SHADER_CAPTURE_RESOURCE_UNCERTAIN_WRITE,
                                 resource_kind, 0, opaque);
}

static inline void pgraph_shader_resource_write(
    PGRAPHState *pg, uint32_t event_kind, uint64_t owner, uint64_t bytes,
    uint32_t resource_kind, bool opaque, uint64_t source_owner,
    uint64_t source_bytes, uint32_t source_kind, bool source_opaque)
{
    uint64_t token = pgraph_shader_resource_begin(pg, event_kind);
    pgraph_shader_resource_stage_write(token, owner, bytes, resource_kind,
                                       opaque, source_owner, source_bytes,
                                       source_kind, source_opaque);
    pgraph_shader_resource_finish(token);
}

static inline void pgraph_shader_resource_release(PGRAPHState *pg,
                                                  uint64_t owner,
                                                  uint64_t bytes, uint32_t kind,
                                                  bool opaque)
{
    uint64_t token =
        pgraph_shader_resource_begin(pg, XEMU_SHADER_CAPTURE_ALLOCATION);
    pgraph_shader_resource_stage(token, owner, bytes,
                                 XEMU_SHADER_CAPTURE_RESOURCE_RELEASE, kind, 0,
                                 opaque);
    pgraph_shader_resource_finish(token);
}

#endif
