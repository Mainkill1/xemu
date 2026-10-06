/* Diagnostic only: measure pool bookkeeping without driver operations. */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/gl/texture.c"
#include "hw/xbox/nv2a/pgraph/gl/texture-stage.c"
NV2AStats g_nv2a_stats;
__typeof__(xemu_tweaks_active) xemu_tweaks_active;
static NV2AState device;
static PGRAPHGLState renderer;
static unsigned int deletes;
static void GLAPIENTRY no_driver_delete(GLsizei n, const GLuint *names)
{
    deletes += n;
}
static uint64_t clock_ns(void)
{
    struct timespec value;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value);
    return (uint64_t)value.tv_sec * 1000000000 + value.tv_nsec;
}
int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 3);
    unsigned layouts = strtoul(argv[1], NULL, 10);
    unsigned count = strtoul(argv[2], NULL, 10);
    g_assert_true(layouts > 0 && layouts <= 257);
    device.pgraph.gl_renderer_state = &renderer;
    pgraph_gl_init_textures(&device);
    epoxy_glDeleteTextures = no_driver_delete;
    TexStorageKey keys[257];
    for (unsigned i = 0; i < layouts; i++) {
        keys[i] = (TexStorageKey){ .target = GL_TEXTURE_2D,
            .internal_format = GL_R8, .width = 8 + i * 4, .height = 16,
            .depth = 1, .levels = 1 };
    }
    unsigned name = 0, misses = 0;
    uint64_t start = clock_ns();
    for (unsigned i = 0; i < count; i++) {
        TexStorageKey *key = &keys[i % layouts];
        GLuint texture = tex_pool_get(&renderer, key);
        if (!texture) {
            texture = ++name;
            misses++;
        }
        tex_pool_put(&renderer, key, texture);
    }
    uint64_t duration = clock_ns() - start;
    printf("{\"layouts\":%u,\"operations\":%u,\"cpu_ns\":%" PRIu64
           ",\"misses\":%u,\"no_driver_deletes\":%u}\n",
           layouts, count, duration, misses, deletes);
    pgraph_gl_finalize_textures(&device.pgraph);
    return 0;
}
