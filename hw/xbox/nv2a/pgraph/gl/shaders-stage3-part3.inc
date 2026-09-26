static PGRAPHGLOverrideProgram *pgraph_gl_override_program_build(
    PGRAPHGLState *renderer, PGRAPHGLOverrideRuntime *runtime,
    const ShaderState *state, const XemuShaderOverridePolicy *policy)
{
    uint64_t replacement_id = policy->replacement_id;
    uint64_t replacement_revision = policy->replacement_revision;
    XemuShaderReplacementSource source = { 0 };
    const char *fragment_source = NULL;

    if (policy->action == XEMU_SHADER_OVERRIDE_ACTION_HIGHLIGHT) {
        replacement_id = PGRAPH_GL_HIGHLIGHT_REPLACEMENT_ID;
        replacement_revision = PGRAPH_GL_HIGHLIGHT_REVISION;
        fragment_source = pgraph_gl_highlight_fragment_source;
    } else if (policy->action == XEMU_SHADER_OVERRIDE_ACTION_REPLACEMENT) {
        if (!xemu_shader_override_acquire_replacement(
                replacement_id, replacement_revision,
                XEMU_SHADER_OVERRIDE_BACKEND_OPENGL, &source)) {
            return NULL;
        }
        if (strcmp(source.entry_point, "main") != 0) {
            xemu_shader_override_release_replacement(&source);
            return NULL;
        }
        fragment_source = (const char *)source.data;
    } else {
        return NULL;
    }

    PGRAPHGLOverrideProgram *existing =
        pgraph_gl_override_program_find_exact(
            runtime, state, policy->action, replacement_id,
            replacement_revision);
    if (existing) {
        xemu_shader_override_release_replacement(&source);
        if (existing->program) {
            return existing;
        }
        return pgraph_gl_override_fallback_program(
            renderer, runtime, state, policy->action, replacement_id);
    }
    if (!pgraph_gl_override_make_room(runtime)) {
        fprintf(stderr,
                "nv2a: OpenGL replacement cache has no evictable entry\n");
        xemu_shader_override_release_replacement(&source);
        return pgraph_gl_override_fallback_program(
            renderer, runtime, state, policy->action, replacement_id);
    }

    PGRAPHGLOverrideProgram *entry =
        g_new0(PGRAPHGLOverrideProgram, 1);
    entry->state = *state;
    entry->action = policy->action;
    entry->replacement_id = replacement_id;
    entry->replacement_revision = replacement_revision;
    entry->last_use = ++runtime->use_clock;

    GLuint fragment = pgraph_gl_compile_override_fragment(
        fragment_source, &entry->error);
    if (!fragment) {
        fprintf(stderr,
                "nv2a: OpenGL shader replacement compile failed: %s\n",
                entry->error ? entry->error : "unknown error");
        g_ptr_array_add(runtime->programs, entry);
        xemu_shader_override_release_replacement(&source);
        return pgraph_gl_override_fallback_program(
            renderer, runtime, state, policy->action, replacement_id);
    }

    GLuint program = glCreateProgram();
    bool need_geometry_shader = pgraph_glsl_need_geom(&state->geom);
    ShaderModuleCacheKey key;
    if (need_geometry_shader) {
        memset(&key, 0, sizeof(key));
        key.kind = GL_GEOMETRY_SHADER;
        key.geom.state = state->geom;
        glAttachShader(program, get_shader_module_for_key(renderer, &key));
    }
    memset(&key, 0, sizeof(key));
    key.kind = GL_VERTEX_SHADER;
    key.vsh.state = state->vsh;
    key.vsh.glsl_opts.prefix_outputs = need_geometry_shader;
    glAttachShader(program, get_shader_module_for_key(renderer, &key));
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        glUseProgram(program);
        pgraph_gl_override_set_sampler_uniforms(program);
        glValidateProgram(program);
        GLint valid = GL_FALSE;
        glGetProgramiv(program, GL_VALIDATE_STATUS, &valid);
        linked = valid;
    }
    if (linked != GL_TRUE) {
        entry->error = pgraph_gl_copy_program_log(program);
        fprintf(stderr,
                "nv2a: OpenGL shader replacement link/validation failed: %s\n",
                entry->error ? entry->error : "unknown error");
        glDeleteProgram(program);
        glDeleteShader(fragment);
        g_ptr_array_add(runtime->programs, entry);
        xemu_shader_override_release_replacement(&source);
        return pgraph_gl_override_fallback_program(
            renderer, runtime, state, policy->action, replacement_id);
    }

    glDeleteShader(fragment);
    entry->program = program;
    pgraph_gl_override_update_uniform_locs(program, &entry->uniform_locs);
    g_ptr_array_add(runtime->programs, entry);
    xemu_shader_override_release_replacement(&source);
    return entry;
}

static void pgraph_gl_override_update_uniforms(
    PGRAPHState *pg, const PGRAPHGLOverrideUniformLocs *locs)
{
    PGRAPHGLState *renderer = pg->gl_renderer_state;
    VshUniformValues vsh_values;
    pgraph_glsl_set_vsh_uniform_values(pg, &renderer->shader_binding->state.vsh,
                                      locs->vsh, &vsh_values);
    apply_uniform_updates(VshUniformInfo, (int *)locs->vsh,
                          &vsh_values, VshUniform__COUNT);

    PshUniformValues psh_values;
    pgraph_glsl_set_psh_uniform_values(pg, locs->psh, &psh_values);
    for (int i = 0; i < NV2A_MAX_TEXTURES; ++i) {
        if (renderer->texture_binding[i]) {
            psh_values.texScale[i] = renderer->texture_binding[i]->scale;
        }
    }
    apply_uniform_updates(PshUniformInfo, (int *)locs->psh,
                          &psh_values, PshUniform__COUNT);
}

static bool pgraph_gl_policy_wants_override(
    const XemuShaderOverridePolicy *policy)
{
    return policy->action == XEMU_SHADER_OVERRIDE_ACTION_REPLACEMENT ||
           policy->action == XEMU_SHADER_OVERRIDE_ACTION_HIGHLIGHT;
}
