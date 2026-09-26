/*
 * NV2A OpenGL Shader Browser Stage 3 draw integration
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* Keep the established draw implementation and replace only the public flush
 * and draw-end boundaries. This preserves command consumption, visibility
 * queries, surface bookkeeping, and existing compatibility suppression. */
#define pgraph_gl_draw_end pgraph_gl_draw_end_stage2
#define pgraph_gl_flush_draw pgraph_gl_flush_draw_stage2
#include "draw.c"
#undef pgraph_gl_draw_end
#undef pgraph_gl_flush_draw

void pgraph_gl_draw_end(NV2AState *d);
void pgraph_gl_flush_draw(NV2AState *d);
bool pgraph_gl_shader_override_program_active(PGRAPHState *pg);
uint32_t pgraph_gl_shader_override_effective_action(PGRAPHState *pg);
void pgraph_gl_shader_override_prepare_draw(
    PGRAPHState *pg, const XemuShaderOverrideDrawFacts *facts);

static void pgraph_gl_override_draw_facts(
    PGRAPHState *pg, XemuShaderOverrideDrawFacts *facts)
{
    memset(facts, 0, sizeof(*facts));
    facts->primitive_mode = pg->primitive_mode;
    facts->available_mask = XEMU_SHADER_OVERRIDE_DRAW_CONDITION_PRIMITIVE;

    if (pg->inline_elements_length) {
        uint32_t min_element = UINT32_MAX;
        uint32_t max_element = 0;
        for (int i = 0; i < pg->inline_elements_length; ++i) {
            min_element = MIN(min_element, pg->inline_elements[i]);
            max_element = MAX(max_element, pg->inline_elements[i]);
        }
        facts->element_count = pg->inline_elements_length;
        facts->min_element = min_element;
        facts->max_element = max_element;
        facts->available_mask |=
            XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_COUNT |
            XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_RANGE;
        return;
    }

    if (pg->draw_arrays_length) {
        uint64_t total = 0;
        for (unsigned int i = 0; i < pg->draw_arrays_length; ++i) {
            total += pg->draw_arrays_count[i];
        }
        facts->element_count = total > UINT32_MAX ? UINT32_MAX : total;
        facts->min_element = pg->draw_arrays_min_start;
        facts->max_element = pg->draw_arrays_max_count ?
            pg->draw_arrays_max_count - 1 : 0;
        facts->available_mask |=
            XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_COUNT |
            XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_RANGE;
        return;
    }

    if (pg->inline_buffer_length) {
        facts->element_count = pg->inline_buffer_length;
        facts->min_element = 0;
        facts->max_element = pg->inline_buffer_length - 1;
        facts->available_mask |=
            XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_COUNT |
            XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_RANGE;
    }
}

static bool pgraph_gl_override_skip_draw(PGRAPHState *pg)
{
    PGRAPHGLState *renderer = pg->gl_renderer_state;
    if (!renderer || !renderer->shader_binding) {
        return false;
    }
    const XemuShaderOverridePolicy *policy =
        &renderer->shader_binding->browser.opengl_policy;
    if (policy->action != XEMU_SHADER_OVERRIDE_ACTION_SKIP_DRAW) {
        return false;
    }
    XemuShaderOverrideDrawFacts facts;
    pgraph_gl_override_draw_facts(pg, &facts);
    return xemu_shader_override_policy_matches_draw(policy, &facts);
}

static uint32_t pgraph_gl_override_observation_route(PGRAPHState *pg)
{
    if (pgraph_gl_shader_override_program_active(pg)) {
        return XEMU_SHADER_BROWSER_ROUTE_REPLACEMENT;
    }
    return XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
}

void pgraph_gl_flush_draw(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *renderer = pg->gl_renderer_state;

    if (!renderer->draw_lifecycle.prepared) {
        return;
    }

    if (pgraph_gl_override_skip_draw(pg)) {
        pgraph_gl_draw_lifecycle_record(&renderer->draw_lifecycle,
                                        PGRAPH_GL_DRAW_EMPTY);
        pgraph_shader_browser_record_draw(
            &pg->shader_browser_observations,
            &renderer->shader_binding->browser, pg->frame_time,
            XEMU_SHADER_BROWSER_ROUTE_DISABLED);
        return;
    }

    if (renderer->shader_binding &&
        renderer->shader_binding->browser.opengl_policy.draw_condition_mask) {
        XemuShaderOverrideDrawFacts facts;
        pgraph_gl_override_draw_facts(pg, &facts);
        pgraph_gl_shader_override_prepare_draw(pg, &facts);
    }

    PGRAPHGLDrawResult result = pgraph_gl_flush_draw_internal(d);
    pgraph_gl_draw_lifecycle_record(&renderer->draw_lifecycle, result);
    if (result == PGRAPH_GL_DRAW_SUBMITTED && renderer->shader_binding) {
        pgraph_shader_browser_record_draw(
            &pg->shader_browser_observations,
            &renderer->shader_binding->browser, pg->frame_time,
            pgraph_gl_override_observation_route(pg));
    }
}

void pgraph_gl_draw_end(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *renderer = pg->gl_renderer_state;

    if (renderer->draw_lifecycle.prepared) {
        pgraph_gl_flush_draw(d);
    }

    /* A skipped draw still closes an active visibility query. The resulting
     * zero samples preserve report/command progress without dirtying surfaces. */
    if (pgraph_gl_draw_lifecycle_take_query(&renderer->draw_lifecycle)) {
        nv2a_profile_inc_counter(NV2A_PROF_QUERY);
        glEndQuery(GL_SAMPLES_PASSED);
    }

    pgraph_gl_complete_draw_lifecycle(
        pg, renderer, renderer->draw_lifecycle.result,
        renderer->draw_lifecycle.color_write,
        renderer->draw_lifecycle.zeta_write,
        renderer->draw_lifecycle.color_dirty,
        renderer->draw_lifecycle.zeta_dirty);
    pgraph_gl_draw_lifecycle_reset(&renderer->draw_lifecycle);
    NV2A_GL_DGROUP_END();
}
