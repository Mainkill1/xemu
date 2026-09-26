/*
 * NV2A OpenGL Shader Browser Stage 3 integration
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* Keep the mature Stage 1/2 implementation in one translation unit so this
 * integration can reuse its module cache, uniform metadata, and state
 * extraction without exporting renderer-private helpers. */
#define pgraph_gl_bind_shaders pgraph_gl_bind_shaders_stage2
#define pgraph_gl_init_shaders pgraph_gl_init_shaders_stage2
#define pgraph_gl_finalize_shaders pgraph_gl_finalize_shaders_stage2
#include "shaders.c"
#undef pgraph_gl_bind_shaders
#undef pgraph_gl_init_shaders
#undef pgraph_gl_finalize_shaders

#define PGRAPH_GL_OVERRIDE_PROGRAM_LIMIT 256
#define PGRAPH_GL_OVERRIDE_ERROR_LIMIT 4096
#define PGRAPH_GL_HIGHLIGHT_REPLACEMENT_ID UINT64_C(0x58454d55484c4954)
#define PGRAPH_GL_HIGHLIGHT_REVISION UINT64_C(1)

typedef struct PGRAPHGLOverrideUniformLocs {
    PshUniformLocs psh;
    VshUniformLocs vsh;
} PGRAPHGLOverrideUniformLocs;

typedef struct PGRAPHGLOverrideProgram {
    ShaderState state;
    uint32_t action;
    uint64_t replacement_id;
    uint64_t replacement_revision;
    GLuint program;
    PGRAPHGLOverrideUniformLocs uniform_locs;
    char *error;
    uint64_t last_use;
} PGRAPHGLOverrideProgram;

typedef struct PGRAPHGLOverrideRuntime {
    PGRAPHGLState *renderer;
    GPtrArray *programs;
    GLuint active_program;
    const void *active_uniform_key;
    uint32_t active_action;
    bool active_custom_program;
    PGRAPHGLOverrideProgram *active_override_program;
    char active_override_error[256];
    uint64_t use_clock;
} PGRAPHGLOverrideRuntime;

static GPtrArray *pgraph_gl_override_runtimes;

static const char pgraph_gl_highlight_fragment_source[] =
    "#version 400 core\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main() { fragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n";

static void pgraph_gl_override_program_free(gpointer opaque)
{
    PGRAPHGLOverrideProgram *program = opaque;
    if (!program) {
        return;
    }
    if (program->program) {
        glDeleteProgram(program->program);
    }
    g_free(program->error);
    g_free(program);
}

static void pgraph_gl_override_runtime_free(gpointer opaque)
{
    PGRAPHGLOverrideRuntime *runtime = opaque;
    if (!runtime) {
        return;
    }
    if (runtime->programs) {
        g_ptr_array_free(runtime->programs, true);
    }
    g_free(runtime);
}

static PGRAPHGLOverrideRuntime *pgraph_gl_override_runtime_find(
    PGRAPHGLState *renderer)
{
    if (!pgraph_gl_override_runtimes) {
        return NULL;
    }
    for (guint i = 0; i < pgraph_gl_override_runtimes->len; ++i) {
        PGRAPHGLOverrideRuntime *runtime =
            g_ptr_array_index(pgraph_gl_override_runtimes, i);
        if (runtime->renderer == renderer) {
            return runtime;
        }
    }
    return NULL;
}

static PGRAPHGLOverrideRuntime *pgraph_gl_override_runtime_get(
    PGRAPHGLState *renderer)
{
    PGRAPHGLOverrideRuntime *runtime =
        pgraph_gl_override_runtime_find(renderer);
    if (runtime) {
        return runtime;
    }
    if (!pgraph_gl_override_runtimes) {
        pgraph_gl_override_runtimes =
            g_ptr_array_new_with_free_func(pgraph_gl_override_runtime_free);
    }
    runtime = g_new0(PGRAPHGLOverrideRuntime, 1);
    runtime->renderer = renderer;
    runtime->programs =
        g_ptr_array_new_with_free_func(pgraph_gl_override_program_free);
    g_ptr_array_add(pgraph_gl_override_runtimes, runtime);
    return runtime;
}

static void pgraph_gl_override_runtime_remove(PGRAPHGLState *renderer)
{
    if (!pgraph_gl_override_runtimes) {
        return;
    }
    for (guint i = 0; i < pgraph_gl_override_runtimes->len; ++i) {
        PGRAPHGLOverrideRuntime *runtime =
            g_ptr_array_index(pgraph_gl_override_runtimes, i);
        if (runtime->renderer == renderer) {
            g_ptr_array_remove_index(pgraph_gl_override_runtimes, i);
            break;
        }
    }
    if (pgraph_gl_override_runtimes->len == 0) {
        g_ptr_array_free(pgraph_gl_override_runtimes, true);
        pgraph_gl_override_runtimes = NULL;
    }
}

static char *pgraph_gl_copy_shader_log(GLuint shader)
{
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    length = CLAMP(length, 1, PGRAPH_GL_OVERRIDE_ERROR_LIMIT);
    char *log = g_malloc0(length + 1);
    glGetShaderInfoLog(shader, length, NULL, log);
    return log;
}

static char *pgraph_gl_copy_program_log(GLuint program)
{
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    length = CLAMP(length, 1, PGRAPH_GL_OVERRIDE_ERROR_LIMIT);
    char *log = g_malloc0(length + 1);
    glGetProgramInfoLog(program, length, NULL, log);
    return log;
}

static GLuint pgraph_gl_compile_override_fragment(const char *source,
                                                  char **error)
{
    if (!source || !source[0]) {
        *error = g_strdup("replacement fragment source is empty");
        return 0;
    }
    GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        *error = pgraph_gl_copy_shader_log(shader);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void pgraph_gl_override_set_sampler_uniforms(GLuint program)
{
    for (int i = 0; i < NV2A_MAX_TEXTURES; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "texSamp%d", i);
        GLint location = glGetUniformLocation(program, name);
        if (location >= 0) {
            glUniform1i(location, i);
        }
    }
}

static void pgraph_gl_override_update_uniform_locs(
    GLuint program, PGRAPHGLOverrideUniformLocs *locs)
{
    char tmp[64];
    for (size_t i = 0; i < ARRAY_SIZE(locs->vsh); ++i) {
        const char *name = VshUniformInfo[i].name;
        if (VshUniformInfo[i].count > 1) {
            snprintf(tmp, sizeof(tmp), "%s[0]", name);
            name = tmp;
        }
        locs->vsh[i] = glGetUniformLocation(program, name);
    }
    for (size_t i = 0; i < ARRAY_SIZE(locs->psh); ++i) {
        const char *name = PshUniformInfo[i].name;
        if (PshUniformInfo[i].count > 1) {
            snprintf(tmp, sizeof(tmp), "%s[0]", name);
            name = tmp;
        }
        locs->psh[i] = glGetUniformLocation(program, name);
    }
}

static bool pgraph_gl_override_program_key_equal(
    const PGRAPHGLOverrideProgram *entry, const ShaderState *state,
    uint32_t action, uint64_t replacement_id, uint64_t replacement_revision)
{
    return entry->action == action &&
           entry->replacement_id == replacement_id &&
           entry->replacement_revision == replacement_revision &&
           memcmp(&entry->state, state, sizeof(*state)) == 0;
}
