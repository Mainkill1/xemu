void pgraph_gl_init_shaders(PGRAPHState *pg)
{
    pgraph_gl_init_shaders_stage2(pg);
    pgraph_gl_override_runtime_get(pg->gl_renderer_state);
    XemuShaderBrowserScope scope = { 0 };
    xemu_shader_browser_copy_current_scope(&scope);
    xemu_shader_override_set_context(
        scope.title_id, scope.executable_fingerprint_version,
        scope.executable_fingerprint, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL);
}

void pgraph_gl_finalize_shaders(PGRAPHState *pg)
{
    pgraph_gl_override_runtime_remove(pg->gl_renderer_state);
    pgraph_gl_finalize_shaders_stage2(pg);
}

void pgraph_gl_bind_shaders(PGRAPHState *pg)
{
    PGRAPHGLState *renderer = pg->gl_renderer_state;
    PGRAPHGLOverrideRuntime *runtime =
        pgraph_gl_override_runtime_get(renderer);
    ShaderBinding *binding = renderer->shader_binding;
    bool state_changed = !binding ||
        pgraph_glsl_check_shader_state_dirty(pg, &binding->state);
    bool cache_locked = false;

    if (state_changed) {
        ShaderState state = pgraph_glsl_get_shader_state(pg);
        NV2A_GL_DGROUP_BEGIN("%s (%s)", __func__,
                             state.vsh.is_fixed_function ? "FF" : "PROG");
        qemu_mutex_lock(&renderer->shader_cache_lock);
        cache_locked = true;
        uint64_t shader_state_hash =
            fast_hash((uint8_t *)&state, sizeof(state));
        LruNode *node = lru_lookup(&renderer->shader_cache,
                                   shader_state_hash, &state);
        binding = container_of(node, ShaderBinding, node);
        renderer->shader_binding = binding;
        pg->program_data_dirty = false;
    } else {
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND_NOTDIRTY);
    }

    assert(binding);
    if (!binding->browser.count) {
        pgraph_shader_browser_publish_binding(
            &binding->state,
            pgraph_glsl_need_geom(&binding->state.geom),
            &binding->browser);
    } else {
        pgraph_shader_browser_refresh_binding_scope(
            &binding->state,
            pgraph_glsl_need_geom(&binding->state.geom),
            &binding->browser);
    }

    const XemuShaderOverridePolicy *policy = &binding->browser.opengl_policy;
    if (policy->action == XEMU_SHADER_OVERRIDE_ACTION_SKIP_DRAW) {
        binding->gl_primitive_mode = get_gl_primitive_mode(
            binding->state.geom.polygon_front_mode,
            binding->state.geom.primitive_mode);
        if (cache_locked) {
            qemu_mutex_unlock(&renderer->shader_cache_lock);
            NV2A_GL_DGROUP_END();
        }
        if (state_changed) {
            pgraph_clear_dirty_reg_map(pg);
        }
        runtime->active_custom_program = false;
        runtime->active_action = XEMU_SHADER_OVERRIDE_ACTION_SKIP_DRAW;
        return;
    }
    PGRAPHGLOverrideProgram *override_program = NULL;
    if (pgraph_gl_policy_wants_override(policy)) {
        override_program = pgraph_gl_override_program_build(
            renderer, runtime, &binding->state, policy);
    }

    if (!override_program || !override_program->program) {
        if (!binding->initialized && !pgraph_gl_shader_load_from_memory(binding)) {
            nv2a_profile_inc_counter(NV2A_PROF_SHADER_GEN);
            int64_t shader_compile_start = g_get_monotonic_time();
            generate_shaders(renderer, binding);
            if (xemu_shader_browser_session_collection_enabled()) {
                binding->browser.compile_cpu_ns =
                    (uint64_t)(g_get_monotonic_time() - shader_compile_start) *
                    1000;
                binding->browser.timings_pending = true;
            }
            if (g_config.perf.cache_shaders) {
                pgraph_gl_shader_cache_to_disk(binding);
            }
        }
        assert(binding->initialized);
    } else {
        binding->gl_primitive_mode = get_gl_primitive_mode(
            binding->state.geom.polygon_front_mode,
            binding->state.geom.primitive_mode);
    }

    if (cache_locked) {
        qemu_mutex_unlock(&renderer->shader_cache_lock);
        NV2A_GL_DGROUP_END();
    }

    GLuint selected_program = override_program && override_program->program ?
        override_program->program : binding->gl_program;
    const void *selected_uniform_key =
        override_program && override_program->program ?
            (const void *)&override_program->uniform_locs :
            (const void *)&binding->uniform_locs;
    if (runtime->active_program != selected_program ||
        runtime->active_uniform_key != selected_uniform_key) {
        glUseProgram(selected_program);
        runtime->active_program = selected_program;
        runtime->active_uniform_key = selected_uniform_key;
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND);
    }
    runtime->active_custom_program =
        override_program && override_program->program;
    runtime->active_action = runtime->active_custom_program ?
        policy->action : XEMU_SHADER_OVERRIDE_ACTION_NORMAL;

    if (state_changed) {
        pgraph_clear_dirty_reg_map(pg);
    }
    if (override_program && override_program->program) {
        pgraph_gl_override_update_uniforms(pg,
                                          &override_program->uniform_locs);
    } else {
        update_shader_uniforms(pg, binding);
    }
}

bool pgraph_gl_shader_override_program_active(PGRAPHState *pg)
{
    if (!pg || !pg->gl_renderer_state) {
        return false;
    }
    PGRAPHGLOverrideRuntime *runtime =
        pgraph_gl_override_runtime_find(pg->gl_renderer_state);
    return runtime && runtime->active_custom_program;
}

uint32_t pgraph_gl_shader_override_effective_action(PGRAPHState *pg)
{
    if (!pg || !pg->gl_renderer_state) {
        return XEMU_SHADER_OVERRIDE_ACTION_NORMAL;
    }
    PGRAPHGLOverrideRuntime *runtime =
        pgraph_gl_override_runtime_find(pg->gl_renderer_state);
    return runtime ? runtime->active_action :
                     XEMU_SHADER_OVERRIDE_ACTION_NORMAL;
}

void pgraph_gl_shader_override_prepare_draw(
    PGRAPHState *pg, const XemuShaderOverrideDrawFacts *facts)
{
    PGRAPHGLState *renderer = pg->gl_renderer_state;
    ShaderBinding *binding = renderer->shader_binding;
    if (!binding || !facts) {
        return;
    }
    const XemuShaderOverridePolicy *policy = &binding->browser.opengl_policy;
    if ((!pgraph_gl_policy_wants_override(policy) &&
         policy->action != XEMU_SHADER_OVERRIDE_ACTION_SKIP_DRAW) ||
        !policy->draw_condition_mask) {
        return;
    }
    if (xemu_shader_override_policy_matches_draw(policy, facts)) {
        if (policy->action == XEMU_SHADER_OVERRIDE_ACTION_SKIP_DRAW) {
            return;
        }
        if (pgraph_gl_shader_override_effective_action(pg) ==
            XEMU_SHADER_OVERRIDE_ACTION_NORMAL) {
            pgraph_gl_bind_shaders(pg);
        }
        return;
    }

    /* A restricted replacement may be active for the binding, but this draw
     * must use the normal program. Prepare it only when the restriction
     * rejects a draw, preserving the replacement's ability to bypass a bad
     * original pixel shader on matching draws. */
    if (!binding->initialized) {
        qemu_mutex_lock(&renderer->shader_cache_lock);
        if (!pgraph_gl_shader_load_from_memory(binding)) {
            generate_shaders(renderer, binding);
            if (g_config.perf.cache_shaders) {
                pgraph_gl_shader_cache_to_disk(binding);
            }
        }
        qemu_mutex_unlock(&renderer->shader_cache_lock);
    }
    PGRAPHGLOverrideRuntime *runtime =
        pgraph_gl_override_runtime_get(renderer);
    if (runtime->active_program != binding->gl_program) {
        glUseProgram(binding->gl_program);
        runtime->active_program = binding->gl_program;
        runtime->active_uniform_key = &binding->uniform_locs;
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND);
    }
    runtime->active_custom_program = false;
    runtime->active_action = XEMU_SHADER_OVERRIDE_ACTION_NORMAL;
    update_shader_uniforms(pg, binding);
}
