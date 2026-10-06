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

static TextureBinding *bench_generate(TextureShape shape, const uint8_t *input)
{
#ifdef NV2A_GL_TEX_POOL_BUCKETS
    TextureBinding *binding = generate_texture(&renderer, shape, input, NULL);
#ifdef BENCH_POOL_BYPASS
    binding->release_texture = NULL;
#endif
    return binding;
#else
    return generate_texture(shape, input, NULL);
#endif
}
static PFNGLGENTEXTURESPROC real_gen;
static PFNGLDELETETEXTURESPROC real_delete;

static void GLAPIENTRY counted_gen(GLsizei n, GLuint *names)
{
    gen_count += n;
    real_gen(n, names);
}

static void GLAPIENTRY counted_delete(GLsizei n, const GLuint *names)
{
    delete_count += n;
    real_delete(n, names);
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
    glDeleteTextures(1, &warmup);
    real_gen = epoxy_glGenTextures;
    real_delete = epoxy_glDeleteTextures;
    epoxy_glGenTextures = counted_gen;
    epoxy_glDeleteTextures = counted_delete;
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
#ifdef BENCH_PIN_RETAINED
    TextureBinding *pinned[256];
    for (unsigned i = 0; i < 256; i++) {
        shape.width = 8 + 4 * i;
        shape.height = 16;
        shape.pitch = shape.width;
        pinned[i] = bench_generate(shape, input);
    }
#endif
    glFinish();
    gen_count = delete_count = 0;
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
    g_assert_cmpint(glGetError(), ==, GL_NO_ERROR);
    uint64_t timed_gen = gen_count, timed_delete = delete_count;
    /* Validate fresh production uploads outside the measured interval. */
    TextureBinding *binding = bench_generate(shape, input);
    uint8_t output[sizeof(input)];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, output);
    g_assert_cmpmem(input, shape.width * shape.height,
                    output, shape.width * shape.height);
    pgraph_gl_texture_binding_destroy(binding);
#ifdef BENCH_PIN_RETAINED
    for (unsigned i = 0; i < 256; i++) {
        pgraph_gl_texture_binding_destroy(pinned[i]);
    }
#endif
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
