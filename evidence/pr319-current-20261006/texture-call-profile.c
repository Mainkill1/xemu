/* Local diagnostic: production generation/release, no timed validation. */
#include "qemu/osdep.h"
#include <epoxy/egl.h>
#include "hw/xbox/nv2a/pgraph/gl/texture.c"
#include "hw/xbox/nv2a/pgraph/gl/texture-stage.c"

NV2AStats g_nv2a_stats;
__typeof__(xemu_tweaks_active) xemu_tweaks_active;
static NV2AState device;
static PGRAPHGLState renderer;
static uint64_t gen_count, delete_count;
static uint64_t gen_ns, delete_ns, bind_ns, upload_ns, parameter_ns;
static uint64_t now_ns(clockid_t clock);

static TextureBinding *bench_generate(TextureShape shape, const uint8_t *input)
{
#ifdef NV2A_GL_TEX_POOL_BUCKETS
    return generate_texture(&renderer, shape, input, NULL);
#else
    return generate_texture(shape, input, NULL);
#endif
}
static PFNGLGENTEXTURESPROC real_gen;
static PFNGLDELETETEXTURESPROC real_delete;

static void GLAPIENTRY counted_gen(GLsizei n, GLuint *names)
{
    gen_count += n;
    uint64_t start = now_ns(CLOCK_MONOTONIC);
    real_gen(n, names);
    gen_ns += now_ns(CLOCK_MONOTONIC) - start;
}

static void GLAPIENTRY counted_delete(GLsizei n, const GLuint *names)
{
    delete_count += n;
    uint64_t start = now_ns(CLOCK_MONOTONIC);
    real_delete(n, names);
    delete_ns += now_ns(CLOCK_MONOTONIC) - start;
}

static PFNGLBINDTEXTUREPROC real_bind;
static PFNGLTEXIMAGE2DPROC real_upload;
static PFNGLTEXPARAMETERIVPROC real_parameter;
static void GLAPIENTRY profiled_bind(GLenum target, GLuint name)
{
    uint64_t start = now_ns(CLOCK_MONOTONIC);
    real_bind(target, name);
    bind_ns += now_ns(CLOCK_MONOTONIC) - start;
}
static void GLAPIENTRY profiled_upload(GLenum target, GLint level, GLint internal,
    GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
    const void *data)
{
    uint64_t start = now_ns(CLOCK_MONOTONIC);
    real_upload(target, level, internal, width, height, border, format, type, data);
    upload_ns += now_ns(CLOCK_MONOTONIC) - start;
}
static void GLAPIENTRY profiled_parameter(GLenum target, GLenum pname,
    const GLint *value)
{
    uint64_t start = now_ns(CLOCK_MONOTONIC);
    real_parameter(target, pname, value);
    parameter_ns += now_ns(CLOCK_MONOTONIC) - start;
}

static uint64_t now_ns(clockid_t clock)
{
    struct timespec ts;
    g_assert_cmpint(clock_gettime(clock, &ts), ==, 0);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 3);
    unsigned shapes = strtoul(argv[1], NULL, 10);
    unsigned count = strtoul(argv[2], NULL, 10);
    g_assert_true(shapes > 0 && (shapes <= 257 || shapes == 8192) && count > 0);
    device.pgraph.gl_renderer_state = &renderer;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    g_assert_true(display != EGL_NO_DISPLAY);
    g_assert_true(eglInitialize(display, NULL, NULL));
    g_assert_true(eglBindAPI(EGL_OPENGL_API));
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT, EGL_NONE,
    };
    EGLConfig config;
    EGLint available;
    g_assert_true(eglChooseConfig(display, attributes, &config, 1, &available));
    g_assert_cmpint(available, >, 0);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    g_assert_true(context != EGL_NO_CONTEXT);
    g_assert_true(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
    GLuint warmup;
    glGenTextures(1, &warmup);
    glBindTexture(GL_TEXTURE_2D, warmup);
    const uint8_t resolve_pixel = 0;
    const GLint resolve_swizzle[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED,
                 GL_UNSIGNED_BYTE, &resolve_pixel);
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, resolve_swizzle);
    glDeleteTextures(1, &warmup);
    real_gen = epoxy_glGenTextures;
    real_delete = epoxy_glDeleteTextures;
    epoxy_glGenTextures = counted_gen;
    epoxy_glDeleteTextures = counted_delete;
    real_bind = epoxy_glBindTexture;
    real_upload = epoxy_glTexImage2D;
    real_parameter = epoxy_glTexParameteriv;
    epoxy_glBindTexture = profiled_bind;
    epoxy_glTexImage2D = profiled_upload;
    epoxy_glTexParameteriv = profiled_parameter;
    pgraph_gl_init_textures(&device);
    uint8_t input[(8 + 4 * 63) * (16 + 8192 / 64)];
    memset(input, 0x74, sizeof(input));
    TextureShape shape = {
        .dimensionality = 2, .height = 16, .depth = 1, .levels = 1,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8,
    };
    for (unsigned i = 0; i < MIN(shapes, 256); i++) {
        shape.width = 8 + 4 * (shapes == 8192 ? i % 64 : i);
        shape.height = shapes == 8192 ? 16 + i / 64 : 16;
        shape.pitch = shape.width;
        pgraph_gl_texture_binding_destroy(bench_generate(shape, input));
    }
    glFinish();
    gen_count = delete_count = 0;
    gen_ns = delete_ns = bind_ns = upload_ns = parameter_ns = 0;
    uint64_t wall_start = now_ns(CLOCK_MONOTONIC);
    uint64_t cpu_start = now_ns(CLOCK_PROCESS_CPUTIME_ID);
    for (unsigned i = 0; i < count; i++) {
        shape.width = 8 + 4 * (shapes == 8192 ? i % 64 : i % shapes);
        shape.height = shapes == 8192 ? 16 + i / 64 : 16;
        shape.pitch = shape.width;
        input[0] = i & 255;
        pgraph_gl_texture_binding_destroy(bench_generate(shape, input));
    }
    glFinish();
    uint64_t cpu = now_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu_start;
    uint64_t wall = now_ns(CLOCK_MONOTONIC) - wall_start;
    fprintf(stderr, "{\"gen_ns\":%" PRIu64 ",\"delete_ns\":%" PRIu64
        ",\"bind_ns\":%" PRIu64 ",\"upload_ns\":%" PRIu64
        ",\"parameter_ns\":%" PRIu64 "}\n",
        gen_ns, delete_ns, bind_ns, upload_ns, parameter_ns);
    g_assert_cmpint(glGetError(), ==, GL_NO_ERROR);
    uint64_t timed_gen = gen_count, timed_delete = delete_count;
    /* Validate fresh production uploads outside the measured interval. */
    TextureBinding *binding = bench_generate(shape, input);
    uint8_t output[sizeof(input)];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, output);
    g_assert_cmpmem(input, shape.width * shape.height,
                    output, shape.width * shape.height);
    pgraph_gl_texture_binding_destroy(binding);
    pgraph_gl_finalize_textures(&device.pgraph);
    g_assert_cmpint(glGetError(), ==, GL_NO_ERROR);
    printf("{\"shapes\":%u,\"operations\":%u,\"cpu_ns\":%" PRIu64
           ",\"wall_ns\":%" PRIu64 ",\"gen\":%" PRIu64
           ",\"delete\":%" PRIu64 ",\"renderer\":\"%s\"}\n",
           shapes, count, cpu, wall, timed_gen, timed_delete,
           glGetString(GL_RENDERER));
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 0;
}
