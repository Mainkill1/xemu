static bool pgraph_gl_override_make_room(PGRAPHGLOverrideRuntime *runtime)
{
    if (runtime->programs->len < PGRAPH_GL_OVERRIDE_PROGRAM_LIMIT) {
        return true;
    }
    guint candidate = G_MAXUINT;
    uint64_t oldest = UINT64_MAX;
    for (guint i = 0; i < runtime->programs->len; ++i) {
        PGRAPHGLOverrideProgram *entry =
            g_ptr_array_index(runtime->programs, i);
        if (entry->program == runtime->active_program) {
            continue;
        }
        if (!entry->program) {
            candidate = i;
            break;
        }
        if (entry->last_use < oldest) {
            oldest = entry->last_use;
            candidate = i;
        }
    }
    if (candidate == G_MAXUINT) {
        return false;
    }
    g_ptr_array_remove_index(runtime->programs, candidate);
    return runtime->programs->len < PGRAPH_GL_OVERRIDE_PROGRAM_LIMIT;
}

static PGRAPHGLOverrideProgram *pgraph_gl_override_program_find_exact(
    PGRAPHGLOverrideRuntime *runtime, const ShaderState *state,
    uint32_t action, uint64_t replacement_id, uint64_t replacement_revision)
{
    for (guint i = 0; i < runtime->programs->len; ++i) {
        PGRAPHGLOverrideProgram *entry =
            g_ptr_array_index(runtime->programs, i);
        if (pgraph_gl_override_program_key_equal(
                entry, state, action, replacement_id, replacement_revision)) {
            entry->last_use = ++runtime->use_clock;
            return entry;
        }
    }
    return NULL;
}

static PGRAPHGLOverrideProgram *pgraph_gl_override_program_find_last_valid(
    PGRAPHGLOverrideRuntime *runtime, const ShaderState *state,
    uint32_t action, uint64_t replacement_id)
{
    PGRAPHGLOverrideProgram *best = NULL;
    for (guint i = 0; i < runtime->programs->len; ++i) {
        PGRAPHGLOverrideProgram *entry =
            g_ptr_array_index(runtime->programs, i);
        if (!entry->program || entry->action != action ||
            entry->replacement_id != replacement_id ||
            memcmp(&entry->state, state, sizeof(*state)) != 0) {
            continue;
        }
        if (!best || entry->last_use > best->last_use) {
            best = entry;
        }
    }
    if (best) {
        best->last_use = ++runtime->use_clock;
    }
    return best;
}

static PGRAPHGLOverrideProgram *pgraph_gl_override_program_build(
    PGRAPHGLState *renderer, PGRAPHGLOverrideRuntime *runtime,
    const ShaderState *state, const XemuShaderOverridePolicy *policy);

static PGRAPHGLOverrideProgram *pgraph_gl_override_fallback_program(
    PGRAPHGLOverrideRuntime *runtime, const ShaderState *state,
    uint32_t action, uint64_t replacement_id)
{
    PGRAPHGLOverrideProgram *fallback =
        pgraph_gl_override_program_find_last_valid(
            runtime, state, action, replacement_id);
    return fallback;
}
