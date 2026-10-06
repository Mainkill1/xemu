/*
 * NV2A OpenGL texture-stage ownership
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_GL_TEXTURE_STAGE_H
#define HW_XBOX_NV2A_PGRAPH_GL_TEXTURE_STAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <epoxy/gl.h>

/* All fields are uint32_t: the exact key has no padding to hash/compare. */
typedef struct TexStorageKey {
    uint32_t target, internal_format;
    uint32_t width, height, depth, levels;
    uint32_t fixed_mip_size;
} TexStorageKey;

typedef struct TextureBinding {
    unsigned int refcnt;
    int draw_time;
    uint64_t data_hash;
    uint64_t texture_vram_offset;
    uint64_t palette_vram_offset;
    unsigned int scale;
    unsigned int min_filter;
    unsigned int mag_filter;
    uint32_t lod_bias;
    unsigned int addru;
    unsigned int addrv;
    unsigned int addrp;
    uint32_t border_color;
    bool border_color_set;
    GLenum gl_target;
    GLuint gl_texture;
    TexStorageKey storage_key;
    void (*release_texture)(struct TextureBinding *binding);
    void *release_opaque;
} TextureBinding;

void pgraph_gl_texture_binding_destroy(TextureBinding *binding);

/*
 * Surface-to-texture can redefine scaled storage independently of the guest
 * upload layout. Such bindings must not reenter that layout's pool.
 */
static inline void pgraph_gl_texture_binding_invalidate_storage(
    TextureBinding *binding)
{
    binding->storage_key.levels = 0;
}

/* The caller must first select the affected GL texture unit. */
void pgraph_gl_reset_texture_stage(TextureBinding **active_binding);

#endif
