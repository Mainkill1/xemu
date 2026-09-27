/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "debug.h"
#include "renderer.h"
#include "ui/xui/shader-browser-details-bridge.h"

typedef struct GLShaderDetailCollection {
    XemuShaderBrowserDetailRequest request;
    XemuShaderBrowserDetailVariant variants[256];
    ShaderBinding *source_binding;
    size_t variant_count;
    size_t omitted;
} GLShaderDetailCollection;

static void pgraph_gl_collect_shader_detail(Lru *lru, LruNode *node,
                                            void *opaque)
{
    GLShaderDetailCollection *out = opaque;
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    bool matches = false;
    for (uint32_t i = 0; i < binding->browser.count; ++i) {
        const PGRAPHShaderBrowserIdentity *identity =
            &binding->browser.identities[i];
        if (identity->stage == out->request.stage &&
            memcmp(identity->hash, out->request.identity_hash,
                   sizeof(identity->hash)) == 0) {
            matches = true;
            break;
        }
    }
    if (!matches) {
        return;
    }
    if (!out->source_binding) {
        out->source_binding = binding;
    }
    if (!(out->request.flags & 2U)) {
        return;
    }
    if (out->variant_count == ARRAY_SIZE(out->variants)) {
        out->omitted++;
        return;
    }
    XemuShaderBrowserDetailVariant *variant =
        &out->variants[out->variant_count++];
    variant->variant_id = (uintptr_t)binding;
    variant->route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
    variant->readiness = binding->initialized ?
        XEMU_SHADER_BROWSER_READINESS_READY :
        XEMU_SHADER_BROWSER_READINESS_PENDING;
    variant->label = "resident OpenGL program";
    if (binding->initialized) {
        variant->valid_fields |=
            XEMU_SHADER_BROWSER_DETAIL_VALID_PRIMITIVE_MODE;
        variant->primitive_mode = binding->gl_primitive_mode;
    }
    if (binding->browser.compile_cpu_ns) {
        variant->valid_fields |=
            XEMU_SHADER_BROWSER_DETAIL_VALID_COMPILE_TIME;
        variant->compile_time_ns = binding->browser.compile_cpu_ns;
    }
}

/* Called on the render owner thread under pgraph.lock. The cache walk does not
 * touch LRU recency or create/compile/link/validate a GL object. */
static void pgraph_gl_service_shader_details(PGRAPHState *pg)
{
    XemuShaderBrowserDetailRequest request = { 0 };
    if (!xemu_shader_browser_details_try_claim(
            XEMU_SHADER_BROWSER_DETAIL_OPENGL, &request)) {
        return;
    }
    PGRAPHGLState *r = pg->gl_renderer_state;
    GLShaderDetailCollection out = { .request = request };
    qemu_mutex_lock(&r->shader_cache_lock);
    lru_visit_active(&r->shader_cache, pgraph_gl_collect_shader_detail, &out);

    MString *code = NULL;
    if (out.source_binding && (request.flags & 1U)) {
        ShaderState *state = &out.source_binding->state;
        switch (request.stage) {
        case XEMU_SHADER_BROWSER_STAGE_VERTEX:
        case XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION: {
            GenVshGlslOptions opts = { 0 };
            opts.prefix_outputs = pgraph_glsl_need_geom(&state->geom);
            code = pgraph_glsl_gen_vsh(&state->vsh, opts);
            break;
        }
        case XEMU_SHADER_BROWSER_STAGE_PIXEL:
            code = pgraph_glsl_gen_psh(&state->psh, (GenPshGlslOptions){ 0 });
            break;
        case XEMU_SHADER_BROWSER_STAGE_GEOMETRY:
            code = pgraph_glsl_gen_geom(&state->geom,
                                       (GenGeomGlslOptions){ 0 });
            break;
        default:
            break;
        }
    }
    XemuShaderBrowserDetailSource source = { 0 };
    if (code && mstring_get_length(code) <= 4U * 1024U * 1024U) {
        source.stage = request.stage == XEMU_SHADER_BROWSER_STAGE_PIXEL ?
            XEMU_SHADER_BROWSER_DETAIL_SOURCE_FRAGMENT :
            request.stage == XEMU_SHADER_BROWSER_STAGE_GEOMETRY ?
                XEMU_SHADER_BROWSER_DETAIL_SOURCE_GEOMETRY :
                XEMU_SHADER_BROWSER_DETAIL_SOURCE_VERTEX;
        source.kind = XEMU_SHADER_BROWSER_DETAIL_SOURCE_GLSL;
        source.route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
        source.artifact_id = (uintptr_t)out.source_binding;
        source.label = "GLSL regenerated from resident shader state";
        source.text = mstring_get_str(code);
        source.text_size = mstring_get_length(code);
    }
    bool source_omitted = code && !source.text;
    char status[160];
    if (source_omitted || out.omitted) {
        snprintf(status, sizeof(status),
                 "OpenGL detail omitted %zu oversized source and %zu program variants",
                 source_omitted ? (size_t)1 : (size_t)0, out.omitted);
    }
    XemuShaderBrowserDetailLifecycle lifecycle[3] = { 0 };
    size_t lifecycle_count = 0;
    if (out.source_binding && out.source_binding == r->shader_binding) {
        lifecycle[lifecycle_count++] =
            (XemuShaderBrowserDetailLifecycle){
                .frame = pg->frame_time,
                .kind = XEMU_SHADER_BROWSER_DETAIL_EVENT_SELECTED,
                .route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
                .artifact_id = (uintptr_t)out.source_binding,
                .message = "program selected when inspected",
            };
    }
    if (source.text) {
        lifecycle[lifecycle_count++] =
            (XemuShaderBrowserDetailLifecycle){
                .frame = pg->frame_time,
                .kind = XEMU_SHADER_BROWSER_DETAIL_EVENT_SOURCE_GENERATED,
                .route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
                .artifact_id = source.artifact_id,
                .message = "GLSL regenerated from resident state for inspection",
            };
    }
    lifecycle[lifecycle_count++] =
        (XemuShaderBrowserDetailLifecycle){
            .frame = pg->frame_time,
            .kind = XEMU_SHADER_BROWSER_DETAIL_EVENT_REQUEST_COMPLETED,
            .route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED,
            .message = "resident OpenGL cache inspected",
        };
    for (size_t i = 0; i < lifecycle_count; ++i) {
        lifecycle[i].sequence = i + 1;
    }
    XemuShaderBrowserDetailResult result = {
        .request = request,
        .backend = XEMU_SHADER_BROWSER_DETAIL_OPENGL,
        .state = !out.source_binding ?
            XEMU_SHADER_BROWSER_DETAIL_UNAVAILABLE :
            (source_omitted || out.omitted) ?
                XEMU_SHADER_BROWSER_DETAIL_PARTIAL :
                XEMU_SHADER_BROWSER_DETAIL_COMPLETE,
        .status = !out.source_binding ?
            "No resident OpenGL binding for this shader" :
            (source_omitted || out.omitted) ? status :
            "Resident OpenGL binding inspected",
        .sources = source.text ? &source : NULL,
        .source_count = source.text ? 1 : 0,
        .variants = (request.flags & 2U) ? out.variants : NULL,
        .variant_count = (request.flags & 2U) ? out.variant_count : 0,
        .lifecycle = (request.flags & 4U) ? lifecycle : NULL,
        .lifecycle_count = (request.flags & 4U) ? lifecycle_count : 0,
    };
    xemu_shader_browser_details_complete(&result);
    if (code) {
        mstring_unref(code);
    }
    qemu_mutex_unlock(&r->shader_cache_lock);
}

GloContext *g_nv2a_context_render;
GloContext *g_nv2a_context_display;

static void early_context_init(void)
{
    g_nv2a_context_render = glo_context_create();
    g_nv2a_context_display = glo_context_create();

    // Note: Due to use of shared contexts, this must happen after some other
    // context is created so the temporary context will not become the thread
    // context. After destroying the context, some a durable context should be
    // selected.
    GloContext *context = glo_context_create();
    pgraph_gl_determine_gpu_properties();
    glo_context_destroy(context);
    glo_set_current(g_nv2a_context_display);
}

static void pgraph_gl_init(NV2AState *d, Error **errp)
{
    PGRAPHState *pg = &d->pgraph;

    pg->gl_renderer_state = g_malloc0(sizeof(*pg->gl_renderer_state));
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* fire up opengl */
    glo_set_current(g_nv2a_context_render);

#if DEBUG_NV2A_GL
    gl_debug_initialize();
#endif

    /* DXT textures */
    assert(glo_check_extension("GL_EXT_texture_compression_s3tc"));
    /*  Internal RGB565 texture format */
    assert(glo_check_extension("GL_ARB_ES2_compatibility"));

    glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE, r->supported_smooth_line_width_range);
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, r->supported_aliased_line_width_range);

    pgraph_gl_init_surfaces(pg);
    pgraph_gl_init_reports(d);
    pgraph_gl_init_textures(d);
    pgraph_gl_init_buffers(d);
    pgraph_gl_init_shaders(pg);
    pgraph_gl_init_display(d);

    pgraph_gl_update_entire_memory_buffer(d);

    pg->uniform_attrs = 0;
    pg->swizzle_attrs = 0;

    r->supported_extensions.texture_filter_anisotropic =
        glo_check_extension("GL_EXT_texture_filter_anisotropic");
}

static void pgraph_gl_finalize(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    glo_set_current(g_nv2a_context_render);

    pgraph_gl_finalize_surfaces(pg);
    pgraph_gl_finalize_shaders(pg);
    pgraph_gl_finalize_textures(pg);
    pgraph_gl_finalize_reports(pg);
    pgraph_gl_finalize_buffers(pg);
    pgraph_gl_finalize_display(pg);

    glo_set_current(NULL);

    g_free(pg->gl_renderer_state);
    pg->gl_renderer_state = NULL;
}

static void pgraph_gl_flip_stall(NV2AState *d)
{
    NV2A_GL_DFRAME_TERMINATOR();
    glFinish();
}

static void pgraph_gl_flush(NV2AState *d)
{
    pgraph_gl_surface_flush(d);
    pgraph_gl_mark_textures_possibly_dirty(d, 0, memory_region_size(d->vram));
    pgraph_gl_update_entire_memory_buffer(d);
    /* FIXME: Flush more? */

    qatomic_set(&d->pgraph.flush_pending, false);
    qemu_event_set(&d->pgraph.flush_complete);
}

static void pgraph_gl_process_pending(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    if (qatomic_read(&r->downloads_pending) ||
        qatomic_read(&r->download_dirty_surfaces_pending) ||
        qatomic_read(&d->pgraph.sync_pending) ||
        qatomic_read(&d->pgraph.flush_pending) ||
        qatomic_read(&r->shader_cache_writeback_pending) ||
        xemu_shader_browser_details_pending()) {
        qemu_mutex_unlock(&d->pfifo.lock);
        qemu_mutex_lock(&d->pgraph.lock);
        if (qatomic_read(&r->downloads_pending)) {
            pgraph_gl_process_pending_downloads(d);
        }
        if (qatomic_read(&r->download_dirty_surfaces_pending)) {
            pgraph_gl_download_dirty_surfaces(d);
        }
        if (qatomic_read(&d->pgraph.sync_pending)) {
            pgraph_gl_sync(d);
        }
        if (qatomic_read(&d->pgraph.flush_pending)) {
            pgraph_gl_flush(d);
        }
        if (qatomic_read(&r->shader_cache_writeback_pending)) {
            pgraph_gl_shader_write_cache_reload_list(&d->pgraph);
        }
        if (xemu_shader_browser_details_pending()) {
            pgraph_gl_service_shader_details(&d->pgraph);
        }
        qemu_mutex_unlock(&d->pgraph.lock);
        qemu_mutex_lock(&d->pfifo.lock);
    }
}

static void pgraph_gl_pre_savevm_trigger(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qatomic_set(&r->download_dirty_surfaces_pending, true);
    qemu_event_reset(&r->dirty_surfaces_download_complete);
}

static void pgraph_gl_pre_savevm_wait(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qemu_event_wait(&r->dirty_surfaces_download_complete);
}

static void pgraph_gl_pre_shutdown_trigger(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qatomic_set(&r->shader_cache_writeback_pending, true);
    qemu_event_reset(&r->shader_cache_writeback_complete);
}

static void pgraph_gl_pre_shutdown_wait(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qemu_event_wait(&r->shader_cache_writeback_complete);
}

static PGRAPHRenderer pgraph_gl_renderer = {
    .type = CONFIG_DISPLAY_RENDERER_OPENGL,
    .name = "OpenGL",
    .ops = {
        .init = pgraph_gl_init,
        .early_context_init = early_context_init,
        .finalize = pgraph_gl_finalize,
        .clear_report_value = pgraph_gl_clear_report_value,
        .clear_surface = pgraph_gl_clear_surface,
        .draw_begin = pgraph_gl_draw_begin,
        .draw_end = pgraph_gl_draw_end,
        .flip_stall = pgraph_gl_flip_stall,
        .flush_draw = pgraph_gl_flush_draw,
        .get_report = pgraph_gl_get_report,
        .image_blit = pgraph_gl_image_blit,
        .pre_savevm_trigger = pgraph_gl_pre_savevm_trigger,
        .pre_savevm_wait = pgraph_gl_pre_savevm_wait,
        .pre_shutdown_trigger = pgraph_gl_pre_shutdown_trigger,
        .pre_shutdown_wait = pgraph_gl_pre_shutdown_wait,
        .process_pending = pgraph_gl_process_pending,
        .process_pending_reports = pgraph_gl_process_pending_reports,
        .surface_update = pgraph_gl_surface_update,
        .set_surface_scale_factor = pgraph_gl_set_surface_scale_factor,
        .get_surface_scale_factor = pgraph_gl_get_surface_scale_factor,
        .get_framebuffer_surface = pgraph_gl_get_framebuffer_surface,
        .get_gpu_properties = pgraph_gl_get_gpu_properties,
    }
};

static void __attribute__((constructor)) register_renderer(void)
{
    pgraph_renderer_register(&pgraph_gl_renderer);
}
