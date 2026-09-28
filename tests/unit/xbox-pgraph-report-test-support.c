/*
 * Owned-memory support for NV2A report integration tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "xbox-pgraph-report-test-support.h"
#include "hw/xbox/nv2a/nv2a_int.h"

#ifdef XBOX_PGRAPH_REPORT_TEST_USE_PRODUCTION_DMA_LOAD
#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-publication.h"
#include "ui/xui/shader-browser-draw-request.h"

/* This report fixture exercises finish/retirement without Shader Browser. */
void xemu_shader_browser_record_dropped_samples(uint64_t count)
{
    (void)count;
}

void xemu_shader_browser_report_gpu_state(uint32_t backend, int supported,
                                          uint32_t pending)
{
    (void)backend;
    (void)supported;
    (void)pending;
}

void pgraph_shader_browser_publish_binding_timing(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend, uint32_t route,
    uint64_t variant_id, uint64_t frame, uint32_t metric, uint64_t duration_ns,
    uint64_t represented_draws, uint32_t flags)
{
    (void)binding;
    (void)backend;
    (void)route;
    (void)variant_id;
    (void)frame;
    (void)metric;
    (void)duration_ns;
    (void)represented_draws;
    (void)flags;
}

/* The report-only fixture keeps capture disarmed. Collection and readback
 * entry points fail if retirement accidentally reaches them. */

void pgraph_shader_browser_publish_binding_timing_at_context(
    const PGRAPHShaderBrowserBinding *binding, uint32_t backend, uint32_t route,
    uint32_t variant_kind, uint64_t variant_id, uint64_t frame, uint32_t metric,
    uint64_t duration_ns, uint64_t represented_draws, uint32_t flags,
    const XemuShaderBrowserPerformanceContext *context)
{
    (void)binding;
    (void)backend;
    (void)route;
    (void)variant_kind;
    (void)variant_id;
    (void)frame;
    (void)metric;
    (void)duration_ns;
    (void)represented_draws;
    (void)flags;
    (void)context;
    g_assert_not_reached();
}

uint64_t xemu_shader_browser_scope_generation(void)
{
    return 0;
}

int xemu_shader_capture_session_active(void)
{
    return 0;
}

int xemu_shader_capture_session_batch_abort(uint64_t batch, uint32_t outcome,
                                            int32_t backend_result)
{
    (void)outcome;
    (void)backend_result;
    g_assert_cmpuint(batch, ==, 0);
    return 0;
}

uint64_t xemu_shader_capture_session_batch_begin(uint64_t scope_generation,
                                                 uint64_t renderer_epoch)
{
    (void)scope_generation;
    (void)renderer_epoch;
    return 0;
}

int xemu_shader_capture_session_batch_current(uint64_t batch)
{
    g_assert_cmpuint(batch, ==, 0);
    return 0;
}

int xemu_shader_capture_session_batch_hold(uint64_t batch, uint64_t token)
{
    (void)batch;
    (void)token;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_capture_session_batch_record(uint64_t batch, uint64_t token,
                                             uint32_t phase, uint64_t ordinal)
{
    (void)batch;
    (void)token;
    (void)phase;
    (void)ordinal;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_capture_session_batch_retire(uint64_t batch, int completed,
                                             int32_t backend_result)
{
    (void)completed;
    (void)backend_result;
    g_assert_cmpuint(batch, ==, 0);
    return 0;
}

int xemu_shader_capture_session_batch_submit(uint64_t batch, int accepted,
                                             uint64_t queue_ordinal,
                                             int32_t backend_result)
{
    (void)accepted;
    (void)queue_ordinal;
    (void)backend_result;
    g_assert_cmpuint(batch, ==, 0);
    return 0;
}

uint64_t xemu_shader_capture_session_begin_event(uint32_t kind, uint64_t frame,
                                                 uint32_t draw,
                                                 uint64_t submission,
                                                 uint64_t scope_generation,
                                                 uint64_t renderer_epoch)
{
    (void)kind;
    (void)frame;
    (void)draw;
    (void)submission;
    (void)scope_generation;
    (void)renderer_epoch;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_capture_session_describe_command(uint64_t token, uint32_t kind,
                                                 uint64_t source_offset,
                                                 uint64_t destination_offset,
                                                 uint64_t bytes)
{
    (void)token;
    (void)kind;
    (void)source_offset;
    (void)destination_offset;
    (void)bytes;
    g_assert_not_reached();
    return 0;
}

void xemu_shader_capture_session_fail(uint64_t token, const char *reason)
{
    (void)token;
    (void)reason;
    g_assert_not_reached();
}

void xemu_shader_capture_session_fail_budget(uint64_t token, const char *reason)
{
    (void)token;
    (void)reason;
    g_assert_not_reached();
}

int xemu_shader_capture_session_resource(
    uint64_t token, const XemuShaderCaptureResource *resource)
{
    (void)token;
    (void)resource;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_capture_session_resource_read_snapshot(uint64_t token,
                                                       uint32_t kind,
                                                       uint32_t slot,
                                                       const char *blob_name)
{
    (void)token;
    (void)kind;
    (void)slot;
    (void)blob_name;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_capture_session_resource_snapshot(
    uint64_t token, const XemuShaderCaptureResource *resource,
    const char *blob_name)
{
    (void)token;
    (void)resource;
    (void)blob_name;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_capture_session_token(uint64_t token)
{
    g_assert_cmpuint(token, ==, 0);
    return 0;
}

int xemu_shader_draw_request_finish(uint64_t token, int emitted,
                                    uint32_t primitive_mode,
                                    uint32_t vertex_count, uint32_t index_count)
{
    (void)token;
    (void)emitted;
    (void)primitive_mode;
    (void)vertex_count;
    (void)index_count;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_draw_request_inputs_complete(uint64_t token)
{
    (void)token;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_draw_request_stage_blob(uint64_t token,
                                        const XemuShaderDrawBlob *blob)
{
    (void)token;
    (void)blob;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_draw_request_stage_image(uint64_t token, int before,
                                         const XemuShaderDrawImage *image)
{
    (void)token;
    (void)before;
    (void)image;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_draw_request_stage_register(uint64_t token, const char *name,
                                            uint32_t value)
{
    (void)token;
    (void)name;
    (void)value;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_draw_request_stage_texture(uint64_t token,
                                           const XemuShaderDrawTexture *texture)
{
    (void)token;
    (void)texture;
    g_assert_not_reached();
    return 0;
}

int xemu_shader_draw_request_wants_inputs(uint64_t token)
{
    (void)token;
    g_assert_not_reached();
    return 0;
}
#endif

#ifndef XBOX_PGRAPH_REPORT_TEST_USE_PRODUCTION_DMA_LOAD
static unsigned int dma_load_count;
#endif

bool report_test_guarded_buffer_init(ReportTestGuardedBuffer *buffer)
{
#ifdef _WIN32
    DWORD old_protect;

    buffer->page_size = qemu_real_host_page_size();
    buffer->mapping = VirtualAlloc(NULL, 2 * buffer->page_size,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (buffer->mapping == NULL) {
        return false;
    }
    buffer->guard = buffer->mapping + buffer->page_size;
    if (!VirtualProtect(buffer->guard, buffer->page_size, PAGE_NOACCESS,
                        &old_protect)) {
        VirtualFree(buffer->mapping, 0, MEM_RELEASE);
        memset(buffer, 0, sizeof(*buffer));
        return false;
    }
#else
    buffer->page_size = qemu_real_host_page_size();
    buffer->mapping = mmap(NULL, 2 * buffer->page_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buffer->mapping == MAP_FAILED) {
        memset(buffer, 0, sizeof(*buffer));
        return false;
    }
    buffer->guard = buffer->mapping + buffer->page_size;
    if (mprotect(buffer->guard, buffer->page_size, PROT_NONE) != 0) {
        munmap(buffer->mapping, 2 * buffer->page_size);
        memset(buffer, 0, sizeof(*buffer));
        return false;
    }
#endif
    return true;
}

void report_test_guarded_buffer_destroy(ReportTestGuardedBuffer *buffer)
{
    if (buffer->mapping == NULL) {
        return;
    }
#ifdef _WIN32
    VirtualFree(buffer->mapping, 0, MEM_RELEASE);
#else
    munmap(buffer->mapping, 2 * buffer->page_size);
#endif
    memset(buffer, 0, sizeof(*buffer));
}

void report_test_memory_region_set_size(MemoryRegion *region, uint64_t size)
{
    memset(region, 0, sizeof(*region));
    region->size = int128_make64(size);
}

uint64_t memory_region_size(MemoryRegion *region)
{
    return int128_get64(region->size);
}

#ifndef XBOX_PGRAPH_REPORT_TEST_USE_PRODUCTION_DMA_LOAD
DMAObject nv_dma_load(NV2AState *d, hwaddr dma_obj_address)
{
    const uint32_t *dma_obj =
        (const uint32_t *)(d->ramin_ptr + dma_obj_address);
    uint32_t flags = ldl_le_p(dma_obj);
    uint32_t limit = ldl_le_p(dma_obj + 1);
    uint32_t frame = ldl_le_p(dma_obj + 2);

    dma_load_count++;
    return (DMAObject){
        .dma_class = GET_MASK(flags, NV_DMA_CLASS),
        .dma_target = GET_MASK(flags, NV_DMA_TARGET),
        .address = (frame & NV_DMA_ADDRESS) | GET_MASK(flags, NV_DMA_ADJUST),
        .limit = limit,
    };
}

unsigned int report_test_dma_load_count(void)
{
    return dma_load_count;
}

void report_test_reset_dma_load_count(void)
{
    dma_load_count = 0;
}
#endif
