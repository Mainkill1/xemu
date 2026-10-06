/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
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

#include "qemu/osdep.h"
#include "qemu/fast-hash.h"
#include "qemu/xxhash.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "hw/xbox/nv2a/pgraph/s3tc.h"
#include "hw/xbox/nv2a/pgraph/texture.h"
#include "hw/xbox/nv2a/pgraph/texture-layout.h"
#include "qemu/error-report.h"
#include "ui/xemu-tweaks.h"
#include "debug.h"
#include "renderer.h"
#include "texture-source-identity.h"

static TextureBinding *generate_texture(PGRAPHGLState *r, TextureShape s,
                                       const uint8_t *texture_data,
                                       const uint8_t *palette_data);

struct pgraph_texture_possibly_dirty_struct {
    hwaddr addr, end;
};

static void mark_textures_possibly_dirty_visitor(Lru *lru, LruNode *node, void *opaque)
{
    struct pgraph_texture_possibly_dirty_struct *test =
        (struct pgraph_texture_possibly_dirty_struct *)opaque;

    struct TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    if (tnode->binding == NULL || tnode->possibly_dirty) {
        return;
    }

    uintptr_t k_tex_addr = tnode->key.texture_vram_offset;
    uintptr_t k_tex_end = k_tex_addr + tnode->key.texture_length - 1;
    bool overlapping = !(test->addr > k_tex_end || k_tex_addr > test->end);

    if (tnode->key.palette_length > 0) {
        uintptr_t k_pal_addr = tnode->key.palette_vram_offset;
        uintptr_t k_pal_end = k_pal_addr + tnode->key.palette_length - 1;
        overlapping |= !(test->addr > k_pal_end || k_pal_addr > test->end);
    }

    tnode->possibly_dirty |= overlapping;
}

void pgraph_gl_mark_textures_possibly_dirty(NV2AState *d,
    hwaddr addr, hwaddr size)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    hwaddr end = TARGET_PAGE_ALIGN(addr + size) - 1;
    addr &= TARGET_PAGE_MASK;
    assert(end <= memory_region_size(d->vram));

    struct pgraph_texture_possibly_dirty_struct test = {
        .addr = addr,
        .end = end,
    };

    lru_visit_active(&r->texture_cache,
                     mark_textures_possibly_dirty_visitor,
                     &test);
}

static bool check_texture_dirty(NV2AState *d, hwaddr addr, hwaddr size)
{
    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;
    assert(end < memory_region_size(d->vram));
    return memory_region_test_and_clear_dirty(d->vram, addr, end - addr,
                                              DIRTY_MEMORY_NV2A_TEX);
}

// Check if any of the pages spanned by the a texture are dirty.
static bool check_texture_possibly_dirty(NV2AState *d,
                                         hwaddr texture_vram_offset,
                                         unsigned int length,
                                         hwaddr palette_vram_offset,
                                         unsigned int palette_length)
{
    bool possibly_dirty = false;
    if (check_texture_dirty(d, texture_vram_offset, length)) {
        possibly_dirty = true;
        pgraph_gl_mark_textures_possibly_dirty(d, texture_vram_offset, length);
    }
    if (palette_length && check_texture_dirty(d, palette_vram_offset,
                                                     palette_length)) {
        possibly_dirty = true;
        pgraph_gl_mark_textures_possibly_dirty(d, palette_vram_offset,
                                            palette_length);
    }
    return possibly_dirty;
}

static void apply_texture_parameters(PGRAPHGLState *r,
                                     TextureBinding *binding,
                                     const BasicColorFormatInfo *f,
                                     unsigned int dimensionality,
                                     unsigned int filter,
                                     unsigned int address,
                                     bool is_bordered,
                                     uint32_t border_color,
                                     uint32_t max_anisotropy)
{
    unsigned int min_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
    unsigned int mag_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);
    unsigned int lod_bias =
        GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS);
    unsigned int addru = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU);
    unsigned int addrv = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV);
    unsigned int addrp = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRP);

    if (f->linear) {
        /* somtimes games try to set mipmap min filters on linear textures.
             * this could indicate a bug... */
        switch (min_filter) {
        case NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD:
        case NV_PGRAPH_TEXFILTER0_MIN_BOX_TENT_LOD:
            min_filter = NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0;
            break;
        case NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD:
        case NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD:
            min_filter = NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0;
            break;
        }
    }

    if (min_filter != binding->min_filter) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_MIN_FILTER,
                        pgraph_texture_min_filter_gl_map[min_filter]);
        binding->min_filter = min_filter;
    }
    if (mag_filter != binding->mag_filter) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_MAG_FILTER,
                        pgraph_texture_mag_filter_gl_map[mag_filter]);
        binding->mag_filter = mag_filter;
    }
    if (lod_bias != binding->lod_bias) {
        binding->lod_bias = lod_bias;
        glTexParameterf(binding->gl_target, GL_TEXTURE_LOD_BIAS,
                        pgraph_convert_lod_bias_to_float(lod_bias));
    }

    /* Texture wrapping */
    assert(addru < ARRAY_SIZE(pgraph_texture_addr_gl_map));
    if (addru != binding->addru) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_S,
                        pgraph_texture_addr_gl_map[addru]);
        binding->addru = addru;
    }
    bool needs_border_color = binding->addru == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    if (dimensionality > 1) {
        if (addrv != binding->addrv) {
            assert(addrv < ARRAY_SIZE(pgraph_texture_addr_gl_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_T,
                            pgraph_texture_addr_gl_map[addrv]);
            binding->addrv = addrv;
        }
        needs_border_color = needs_border_color || binding->addrv == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    }
    if (dimensionality > 2) {
        if (addrp != binding->addrp) {
            assert(addrp < ARRAY_SIZE(pgraph_texture_addr_gl_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_R,
                            pgraph_texture_addr_gl_map[addrp]);
            binding->addrp = addrp;
        }
        needs_border_color = needs_border_color || binding->addrp == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    }

    if (r->supported_extensions.texture_filter_anisotropic) {
        glTexParameterf(binding->gl_target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        max_anisotropy);
    }

    if (!is_bordered && needs_border_color) {
        if (!binding->border_color_set || binding->border_color != border_color) {
            /* FIXME: Color channels might be wrong order */
            GLfloat gl_border_color[4];
            pgraph_argb_pack32_to_rgba_float(border_color, gl_border_color);
            glTexParameterfv(binding->gl_target, GL_TEXTURE_BORDER_COLOR,
                             gl_border_color);

            binding->border_color_set = true;
            binding->border_color = border_color;
        }
    }
}

void pgraph_gl_bind_textures(NV2AState *d)
{
    int i;
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    NV2A_GL_DGROUP_BEGIN("%s", __func__);

    for (i=0; i<NV2A_MAX_TEXTURES; i++) {
        bool enabled = pgraph_is_texture_enabled(pg, i);
        /* FIXME: What happens if texture is disabled but stage is active? */

        glActiveTexture(GL_TEXTURE0 + i);
        if (!enabled) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glBindTexture(GL_TEXTURE_1D, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_3D, 0);
            continue;
        }

        uint32_t filter = pgraph_reg_r(pg, NV_PGRAPH_TEXFILTER0 + i*4);
        uint32_t address = pgraph_reg_r(pg, NV_PGRAPH_TEXADDRESS0 + i*4);
        uint32_t border_color = pgraph_reg_r(pg, NV_PGRAPH_BORDERCOLOR0 + i*4);
        uint32_t max_anisotropy =
            1 << (GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_TEXCTL0_0 + i*4),
                           NV_PGRAPH_TEXCTL0_0_MAX_ANISOTROPY));

        /* Check for unsupported features */
        if (filter & NV_PGRAPH_TEXFILTER0_ASIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_ASIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_RSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_RSIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_GSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_GSIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_BSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_BSIGNED");

        TextureShape state = pgraph_get_texture_shape(pg, i);
        hwaddr texture_vram_offset, palette_vram_offset;
        size_t length, palette_length;

        length = pgraph_get_texture_length(pg, &state);
        if (!length) {
            error_report("Invalid texture source layout");
            pgraph_gl_reset_texture_stage(&r->texture_binding[i]);
            pg->texture_dirty[i] = true;
            continue;
        }
        texture_vram_offset = pgraph_get_texture_phys_addr(pg, i);
        palette_vram_offset = pgraph_get_texture_palette_phys_addr_length(pg, i, &palette_length);

        assert((texture_vram_offset + length) < memory_region_size(d->vram));
        assert((palette_vram_offset + palette_length)
               < memory_region_size(d->vram));
        bool is_indexed = (state.color_format ==
                NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);
        bool possibly_dirty = false;
        bool possibly_dirty_checked = false;

        SurfaceBinding *surface = pgraph_gl_surface_get(d, texture_vram_offset);
        TextureBinding *tbind = r->texture_binding[i];
        if (!pg->texture_dirty[i] && tbind) {
            bool reusable = false;
            bool source_matches =
                pgraph_gl_texture_source_identity_matches(
                    texture_vram_offset, tbind->texture_vram_offset,
                    is_indexed, palette_vram_offset,
                    tbind->palette_vram_offset);
            if (source_matches && surface &&
                tbind->draw_time == surface->draw_time) {
                reusable = true;
            } else if (source_matches && !surface) {
                possibly_dirty = check_texture_possibly_dirty(
                        d,
                        texture_vram_offset,
                        length,
                        palette_vram_offset,
                        is_indexed ? palette_length : 0);
                possibly_dirty_checked = true;
                reusable = !possibly_dirty;
            }

            if (reusable) {
                glBindTexture(r->texture_binding[i]->gl_target,
                              r->texture_binding[i]->gl_texture);
                apply_texture_parameters(r,
                                         r->texture_binding[i],
                                         &kelvin_color_format_info_map[state.color_format],
                                         state.dimensionality,
                                         filter,
                                         address,
                                         state.border,
                                         border_color,
                                         max_anisotropy);
                continue;
            }
        }

        /*
         * Check active surfaces to see if this texture was a render target
         */
        bool surf_to_tex = false;
        if (surface != NULL) {
            surf_to_tex = pgraph_gl_check_surface_to_texture_compatibility(
                    surface, &state);

            if (surf_to_tex && surface->upload_pending) {
                pgraph_gl_upload_surface_data(d, surface, false);
            }
        }

        if (!surf_to_tex) {
            // FIXME: Restructure to support rendering surfaces to cubemap faces

            // Writeback any surfaces which this texture may index
            hwaddr tex_vram_end = texture_vram_offset + length - 1;
            QTAILQ_FOREACH(surface, &r->surfaces, entry) {
                hwaddr surf_vram_end = surface->vram_addr + surface->size - 1;
                bool overlapping = !(surface->vram_addr >= tex_vram_end
                                     || texture_vram_offset >= surf_vram_end);
                if (overlapping) {
                    pgraph_gl_surface_download_if_dirty(d, surface);
                }
            }
        }

        TextureKey key;
        memset(&key, 0, sizeof(TextureKey));
        key.state = state;
        key.texture_vram_offset = texture_vram_offset;
        key.texture_length = length;
        if (is_indexed) {
            key.palette_vram_offset = palette_vram_offset;
            key.palette_length = palette_length;
        }

        // Search for existing texture binding in cache
        uint64_t tex_binding_hash = fast_hash((uint8_t*)&key, sizeof(key));
        LruNode *found = lru_lookup(&r->texture_cache,
                                     tex_binding_hash, &key);
        TextureLruNode *key_out = container_of(found, TextureLruNode, node);
        possibly_dirty |= (key_out->binding == NULL) || key_out->possibly_dirty;

        if (!surf_to_tex && !possibly_dirty_checked) {
            possibly_dirty |= check_texture_possibly_dirty(
                    d,
                    texture_vram_offset,
                    length,
                    palette_vram_offset,
                    is_indexed ? palette_length : 0);
        }

        // Calculate hash of texture data, if necessary
        void *texture_data = (char*)d->vram_ptr + texture_vram_offset;
        void *palette_data = (char*)d->vram_ptr + palette_vram_offset;

        uint64_t tex_data_hash = 0;
        if (!surf_to_tex && possibly_dirty) {
            tex_data_hash = fast_hash(texture_data, length);
            if (is_indexed) {
                tex_data_hash ^= fast_hash(palette_data, palette_length);
            }
        }

        // Free existing binding, if texture data has changed
        bool must_destroy = (key_out->binding != NULL)
                            && possibly_dirty
                            && (key_out->binding->data_hash != tex_data_hash);
        if (must_destroy) {
            pgraph_gl_texture_binding_destroy(key_out->binding);
            key_out->binding = NULL;
        }

        if (key_out->binding == NULL) {
            // Must create the texture
            uint64_t generations_before = r->tex_pool_log ?
                r->tex_pool_stats.generations : 0;
            key_out->binding =
                generate_texture(r, state, texture_data, palette_data);
            if (!key_out->binding) {
                key_out->possibly_dirty = true;
                pgraph_gl_reset_texture_stage(&r->texture_binding[i]);
                pg->texture_dirty[i] = true;
                continue;
            }
            NV2A_GL_DLABEL(GL_TEXTURE, key_out->binding->gl_texture,
                           "offset: 0x%08" PRIx64 ", format: 0x%02X, "
                           "%u dimensions%s, width: %u, height: %u, depth: %u",
                           (uint64_t)texture_vram_offset, state.color_format,
                           state.dimensionality,
                           state.cubemap ? " (Cubemap)" : "",
                           state.width, state.height, state.depth);

            TextureBinding *outgoing = r->texture_binding[i];
            if (r->tex_pool_log &&
                r->tex_pool_stats.generations != generations_before &&
                outgoing && outgoing->refcnt == 1 &&
                !memcmp(&outgoing->storage_key,
                        &key_out->binding->storage_key,
                        sizeof(TexStorageKey))) {
                r->tex_pool_stats.outgoing_matches++;
            }
            key_out->binding->data_hash = tex_data_hash;
            key_out->binding->texture_vram_offset = texture_vram_offset;
            key_out->binding->palette_vram_offset =
                is_indexed ? palette_vram_offset : 0;
            key_out->binding->scale = 1;
        } else {
            // Saved an upload! Reuse existing texture in graphics memory.
            glBindTexture(key_out->binding->gl_target,
                          key_out->binding->gl_texture);
        }

        key_out->possibly_dirty = false;
        TextureBinding *binding = key_out->binding;
        binding->refcnt++;

        if (surf_to_tex && binding->draw_time < surface->draw_time) {

            trace_nv2a_pgraph_surface_render_to_texture(
                surface->vram_addr, surface->width, surface->height);
            pgraph_gl_render_surface_to_texture(d, surface, binding, &state, i);
            binding->draw_time = surface->draw_time;
            binding->scale = pg->surface_scale_factor;
        }

        apply_texture_parameters(r,
                                 binding,
                                 &kelvin_color_format_info_map[state.color_format],
                                 state.dimensionality,
                                 filter,
                                 address,
                                 state.border,
                                 border_color,
                                 max_anisotropy);

        if (r->texture_binding[i]) {
            if (r->texture_binding[i]->gl_target != binding->gl_target) {
                glBindTexture(r->texture_binding[i]->gl_target, 0);
            }
            pgraph_gl_texture_binding_destroy(r->texture_binding[i]);
        }
        r->texture_binding[i] = binding;
        pg->texture_dirty[i] = false;
    }
    NV2A_GL_DGROUP_END();
}

static enum S3TC_DECOMPRESS_FORMAT
gl_internal_format_to_s3tc_enum(GLint gl_internal_format)
{
    switch (gl_internal_format) {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT1;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT3;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT5;
    default:
        assert(!"Invalid gl_internal_format");
    }
}

static void upload_gl_texture(GLenum gl_target,
                              const TextureShape s,
                              const uint8_t *texture_data,
                              const uint8_t *palette_data)
{
    ColorFormatInfo f = kelvin_color_format_gl_map[s.color_format];
    nv2a_profile_inc_counter(NV2A_PROF_TEX_UPLOAD);

    unsigned int adjusted_width = s.width;
    unsigned int adjusted_height = s.height;
    unsigned int adjusted_pitch = s.pitch;
    unsigned int adjusted_depth = s.depth;
    if (!f.linear && s.border) {
        adjusted_width = MAX(16, adjusted_width * 2);
        adjusted_height = MAX(16, adjusted_height * 2);
        adjusted_pitch = adjusted_width * (s.pitch / s.width);
        adjusted_depth = MAX(16, s.depth * 2);
    }

    switch(gl_target) {
    case GL_TEXTURE_1D:
        assert(!"Invalid 1D gl target texture");
        break;
    case GL_TEXTURE_2D:
        if (f.linear) {
            /* Can't handle strides unaligned to pixels */
            assert(s.pitch % f.bytes_per_pixel == 0);

            uint8_t *converted = pgraph_convert_texture_data(
                s, texture_data, palette_data, adjusted_width, adjusted_height, 1,
                adjusted_pitch, 0, NULL);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,
                          converted ? 0 : adjusted_pitch / f.bytes_per_pixel);
            glTexImage2D(GL_TEXTURE_2D, 0, f.gl_internal_format,
                         adjusted_width, adjusted_height, 0,
                         f.gl_format, f.gl_type,
                         converted ? converted : texture_data);

            if (converted) {
              g_free(converted);
            }

            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            break;
        }
        /* fallthru */
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z: {

        unsigned int width = adjusted_width, height = adjusted_height;

        int level;
        for (level = 0; level < s.levels; level++) {
            width = MAX(width, 1);
            height = MAX(height, 1);

            if (f.gl_format == 0) { /* compressed */
                 // https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression#virtual-size-versus-physical-size
                unsigned int block_size =
                    f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ?
                        8 : 16;
                unsigned int physical_width = (width + 3) & ~3,
                             physical_height = (height + 3) & ~3;

                /*
                 * The host is required to support S3TC (asserted at renderer
                 * init), so hand the DXT blocks straight to GL when no border
                 * fixup is needed. This skips the CPU decompress entirely and
                 * uploads 4-8x less data, which matters a great deal when a
                 * title streams in many textures in one frame.
                 *
                 * This must not be decided per level: mixing compressed and
                 * uncompressed internal formats across the mip chain makes
                 * the texture incomplete and it samples as black. The border
                 * fixup depends only on the texture, not the level, so either
                 * every level takes this path or none does. Levels smaller
                 * than a 4x4 block are fine to upload compressed; imageSize
                 * is computed from the block-aligned size.
                 */
                bool needs_border_fixup =
                    s.cubemap && adjusted_width != s.width;
                if (!needs_border_fixup &&
                    xemu_tweak_enabled(XEMU_TWEAK_GL_NATIVE_S3TC)) {
                    unsigned int image_size = (physical_width / 4) *
                                              (physical_height / 4) *
                                              block_size;
                    glCompressedTexImage2D(gl_target, level,
                                           f.gl_internal_format,
                                           width, height, 0,
                                           image_size, texture_data);
                    texture_data += image_size;
                    width /= 2;
                    height /= 2;
                    continue;
                }

                uint8_t *converted = s3tc_decompress_2d(
                    gl_internal_format_to_s3tc_enum(f.gl_internal_format),
                    texture_data, width, height);
                unsigned int tex_width = width;
                unsigned int tex_height = height;

                if (s.cubemap && adjusted_width != s.width) {
                    // FIXME: Consider preserving the border.
                    // There does not seem to be a way to reference the border
                    // texels in a cubemap, so they are discarded.
                    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 4);
                    glPixelStorei(GL_UNPACK_SKIP_ROWS, 4);
                    tex_width = s.width;
                    tex_height = s.height;
                    if (physical_width == width) {
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, adjusted_width);
                    }
                }

                glTexImage2D(gl_target, level, GL_RGBA, tex_width, tex_height, 0,
                             GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, converted);
                g_free(converted);
                if (s.cubemap && adjusted_width != s.width) {
                    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
                    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
                    if (physical_width == width) {
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    }
                }
                texture_data +=
                    physical_width / 4 * physical_height / 4 * block_size;
            } else {
                unsigned int pitch = width * f.bytes_per_pixel;
                uint8_t *unswizzled = (uint8_t*)g_malloc(height * pitch);
                unswizzle_rect(texture_data, width, height,
                               unswizzled, pitch, f.bytes_per_pixel);
                size_t converted_size = 0;
                uint8_t *converted = pgraph_convert_texture_data(
                    s, unswizzled, palette_data, width, height, 1, pitch, 0,
                    &converted_size);
                uint8_t *pixel_data = converted ? converted : unswizzled;
                unsigned int tex_width = width;
                unsigned int tex_height = height;
                GLint previous_unpack_alignment = 4;
                bool cropped = s.cubemap && adjusted_width != s.width;

                if (cropped) {
                    // FIXME: Consider preserving the border.
                    // There does not seem to be a way to reference the border
                    // texels in a cubemap, so they are discarded.
                    PGRAPHTextureMipCrop crop =
                        pgraph_bordered_texture_mip_crop(
                            s.width, s.height, width, height, level);
                    size_t upload_bytes_per_pixel = f.bytes_per_pixel;
                    if (converted) {
                        size_t pixels = (size_t)width * height;
                        assert(pixels && converted_size >= pixels &&
                               converted_size % pixels == 0);
                        upload_bytes_per_pixel = converted_size / pixels;
                    }
                    glGetIntegerv(GL_UNPACK_ALIGNMENT,
                                  &previous_unpack_alignment);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
                    tex_width = crop.width;
                    tex_height = crop.height;
                    pixel_data += ((size_t)crop.skip_rows * width +
                                   crop.skip_pixels) * upload_bytes_per_pixel;
                }

                glTexImage2D(gl_target, level, f.gl_internal_format, tex_width,
                             tex_height, 0, f.gl_format, f.gl_type,
                             pixel_data);
                if (cropped) {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    glPixelStorei(GL_UNPACK_ALIGNMENT,
                                  previous_unpack_alignment);
                }
                if (converted) {
                    g_free(converted);
                }
                g_free(unswizzled);

                texture_data += width * height * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
        }

        break;
    }
    case GL_TEXTURE_3D: {

        unsigned int width = adjusted_width;
        unsigned int height = adjusted_height;
        unsigned int depth = adjusted_depth;

        assert(f.linear == false);

        int level;
        for (level = 0; level < s.levels; level++) {
            if (f.gl_format == 0) { /* compressed */
                width = MAX(width, 1);
                height = MAX(height, 1);
                unsigned int physical_width = (width + 3) & ~3,
                             physical_height = (height + 3) & ~3;
                depth = MAX(depth, 1);

                unsigned int block_size;
                if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                    block_size = 8;
                } else {
                    block_size = 16;
                }

                size_t texture_size = physical_width/4 * physical_height/4 * depth * block_size;

                uint8_t *converted = s3tc_decompress_3d(
                    gl_internal_format_to_s3tc_enum(f.gl_internal_format),
                    texture_data, width, height, depth);

                glTexImage3D(gl_target, level,  GL_RGBA8,
                             width, height, depth, 0,
                             GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
                             converted);

                g_free(converted);

                texture_data += texture_size;
            } else {
                width = MAX(width, 1);
                height = MAX(height, 1);
                depth = MAX(depth, 1);

                unsigned int row_pitch = width * f.bytes_per_pixel;
                unsigned int slice_pitch = row_pitch * height;
                uint8_t *unswizzled = (uint8_t*)g_malloc(slice_pitch * depth);
                unswizzle_box(texture_data, width, height, depth, unswizzled,
                               row_pitch, slice_pitch, f.bytes_per_pixel);

                uint8_t *converted = pgraph_convert_texture_data(
                    s, unswizzled, palette_data, width, height, depth,
                    row_pitch, slice_pitch, NULL);

                glTexImage3D(gl_target, level, f.gl_internal_format,
                             width, height, depth, 0,
                             f.gl_format, f.gl_type,
                             converted ? converted : unswizzled);

                if (converted) {
                    g_free(converted);
                }
                g_free(unswizzled);

                texture_data += width * height * depth * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
            depth /= 2;
        }
        break;
    }
    default:
        assert(!"Invalid gl target texture type");
        break;
    }
}

static TexStorageKey tex_storage_key(GLenum target, const TextureShape *s,
                                     const ColorFormatInfo *f)
{
    TexStorageKey key = {
        .target = target, .internal_format = f->gl_internal_format,
        .width = s->width, .height = s->height, .depth = s->depth,
        .levels = f->linear ? 1 : s->levels,
    };
    if (!f->linear && s->border && !s->cubemap) {
        key.width = MAX(16, s->width * 2);
        key.height = MAX(16, s->height * 2);
        key.depth = MAX(16, s->depth * 2);
    }
    if (target != GL_TEXTURE_3D) {
        key.depth = 1;
    }
    if (!f->gl_format &&
        (target == GL_TEXTURE_3D || (s->cubemap && s->border) ||
         !xemu_tweak_enabled(XEMU_TWEAK_GL_NATIVE_S3TC))) {
        key.internal_format = target == GL_TEXTURE_3D ? GL_RGBA8 : GL_RGBA;
        /*
         * The current decompressed bordered cube uploader fixes every level
         * to the guest base dimensions. Keep that layout distinct.
         */
        key.fixed_mip_size = s->cubemap && s->border;
    }
    return key;
}

static size_t tex_storage_bytes(const TexStorageKey *key)
{
    size_t bytes = 0;
    size_t width = key->width, height = key->height, depth = key->depth;

    for (unsigned int level = 0; level < key->levels; level++) {
        size_t pixels, level_bytes;
        bool compressed =
            key->internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
            key->internal_format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
            key->internal_format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        size_t w = compressed ? (width + 3) / 4 : width;
        size_t h = compressed ? (height + 3) / 4 : height;
        size_t unit = compressed ?
            (key->internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ?
             8 : 16) :
            8; /* Conservative uncompressed estimate; not driver allocation. */

        if (!pgraph_texture_size_mul(w, h, &pixels) ||
            !pgraph_texture_size_mul(pixels, depth, &pixels) ||
            !pgraph_texture_size_mul(pixels, unit, &level_bytes) ||
            !pgraph_texture_size_add(bytes, level_bytes, &bytes)) {
            return SIZE_MAX;
        }
        if (!key->fixed_mip_size) {
            width = MAX(width / 2, 1);
            height = MAX(height / 2, 1);
            depth = MAX(depth / 2, 1);
        }
    }
    if (key->target == GL_TEXTURE_CUBE_MAP &&
        !pgraph_texture_size_mul(bytes, 6, &bytes)) {
        return SIZE_MAX;
    }
    return bytes;
}

static unsigned int tex_pool_bucket(const TexStorageKey *key)
{
    uint32_t hash = qemu_xxhash7(
        (uint64_t)key->target << 32 | key->internal_format,
        (uint64_t)key->width << 32 | key->height,
        (uint64_t)key->depth << 32 | key->levels, key->fixed_mip_size);
    return hash % NV2A_GL_TEX_POOL_BUCKETS;
}

static void tex_pool_delete(PGRAPHGLState *r, GLuint texture)
{
    glDeleteTextures(1, &texture);
    if (r->tex_pool_log) {
        r->tex_pool_stats.deletions++;
    }
}

static void tex_pool_remove(PGRAPHGLState *r, TexPoolEntry *entry)
{
    QTAILQ_REMOVE(&r->tex_pool_fifo, entry, fifo);
    QLIST_REMOVE(entry, bucket);
    r->tex_pool_count--;
    r->tex_pool_bytes -= entry->bytes;
    QSLIST_INSERT_HEAD(&r->tex_pool_free, entry, free);
}

static GLuint tex_pool_get(PGRAPHGLState *r, const TexStorageKey *key)
{
    TexPoolEntry *entry;

    if (r->tex_pool_log) {
        r->tex_pool_stats.get_calls++;
    }
    QLIST_FOREACH(entry, &r->tex_pool_buckets[tex_pool_bucket(key)], bucket) {
        if (r->tex_pool_log) {
            r->tex_pool_stats.entries_scanned++;
        }
        if (!memcmp(&entry->key, key, sizeof(*key))) {
            GLuint texture = entry->gl_texture;
            tex_pool_remove(r, entry);
            if (r->tex_pool_log) {
                r->tex_pool_stats.hits++;
            }
            return texture;
        }
    }
    if (r->tex_pool_log) {
        r->tex_pool_stats.misses++;
    }
    return 0;
}

static void tex_pool_put(PGRAPHGLState *r, const TexStorageKey *key,
                          GLuint texture)
{
    size_t bytes = tex_storage_bytes(key);

    if (r->tex_pool_log) {
        r->tex_pool_stats.puts++;
    }
    if (!key->levels || bytes > NV2A_GL_TEX_POOL_BYTES) {
        tex_pool_delete(r, texture);
        return;
    }
    while (r->tex_pool_count == NV2A_GL_TEX_POOL_SIZE ||
           bytes > NV2A_GL_TEX_POOL_BYTES - r->tex_pool_bytes) {
        TexPoolEntry *oldest = QTAILQ_FIRST(&r->tex_pool_fifo);
        tex_pool_delete(r, oldest->gl_texture);
        tex_pool_remove(r, oldest);
        if (r->tex_pool_log) {
            r->tex_pool_stats.evictions++;
        }
    }
    TexPoolEntry *entry = QSLIST_FIRST(&r->tex_pool_free);
    QSLIST_REMOVE_HEAD(&r->tex_pool_free, free);
    entry->key = *key;
    entry->gl_texture = texture;
    entry->bytes = bytes;
    QTAILQ_INSERT_TAIL(&r->tex_pool_fifo, entry, fifo);
    QLIST_INSERT_HEAD(&r->tex_pool_buckets[tex_pool_bucket(key)],
                      entry, bucket);
    r->tex_pool_count++;
    r->tex_pool_bytes += bytes;
    if (r->tex_pool_log) {
        r->tex_pool_stats.high_water_entries =
            MAX(r->tex_pool_stats.high_water_entries, r->tex_pool_count);
        r->tex_pool_stats.high_water_bytes =
            MAX(r->tex_pool_stats.high_water_bytes, r->tex_pool_bytes);
    }
}

static void recycle_texture_binding(TextureBinding *binding)
{
    PGRAPHGLState *r = binding->release_opaque;

    tex_pool_put(r, &binding->storage_key, binding->gl_texture);
}

static TextureBinding *generate_texture(PGRAPHGLState *r, TextureShape s,
                                        const uint8_t *texture_data,
                                        const uint8_t *palette_data)
{
    ColorFormatInfo f = kelvin_color_format_gl_map[s.color_format];

    GLenum gl_target;
    if (s.cubemap) {
        assert(f.linear == false);
        assert(s.dimensionality == 2);
        gl_target = GL_TEXTURE_CUBE_MAP;
    } else {
        if (f.linear) {
            gl_target = GL_TEXTURE_2D;
            assert(s.dimensionality == 2);
        } else {
            switch(s.dimensionality) {
            case 1: gl_target = GL_TEXTURE_1D; break;
            case 2: gl_target = GL_TEXTURE_2D; break;
            case 3: gl_target = GL_TEXTURE_3D; break;
            default:
                NV2A_GL_DPRINTF(true, "Invalid texture dimensionality: %u", (unsigned)s.dimensionality);
                assert(!"Invalid texture dimensionality");
                break;
            }
        }
    }

    TexStorageKey storage_key = tex_storage_key(gl_target, &s, &f);
    GLuint gl_texture = tex_pool_get(r, &storage_key);
    if (!gl_texture) {
        glGenTextures(1, &gl_texture);
        if (r->tex_pool_log) {
            r->tex_pool_stats.generations++;
        }
    }

    glBindTexture(gl_target, gl_texture);

    if (gl_target == GL_TEXTURE_CUBE_MAP) {
        size_t length;
        if (!pgraph_calculate_texture_cubemap_face_stride(
                &s, f.gl_format == 0, f.bytes_per_pixel, &length)) {
            error_report("Invalid cubemap source layout");
            glBindTexture(gl_target, 0);
            tex_pool_delete(r, gl_texture);
            return NULL;
        }

        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                          s, texture_data + 0 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                          s, texture_data + 1 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                          s, texture_data + 2 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                          s, texture_data + 3 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                          s, texture_data + 4 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
                          s, texture_data + 5 * length, palette_data);
    } else {
        upload_gl_texture(gl_target, s, texture_data, palette_data);
    }

    /* Linear textures don't support mipmapping */
    if (!f.linear) {
        glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
            s.min_mipmap_level);
        glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
            s.levels - 1);
    }

    const GLint identity_swizzle[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
    bool has_swizzle = f.gl_swizzle_mask[0] || f.gl_swizzle_mask[1] ||
                       f.gl_swizzle_mask[2] || f.gl_swizzle_mask[3];
    glTexParameteriv(gl_target, GL_TEXTURE_SWIZZLE_RGBA,
                     has_swizzle ? (const GLint *)f.gl_swizzle_mask :
                                   identity_swizzle);

    TextureBinding* ret = (TextureBinding *)g_malloc(sizeof(TextureBinding));
    ret->gl_target = gl_target;
    ret->gl_texture = gl_texture;
    ret->refcnt = 1;
    ret->draw_time = 0;
    ret->data_hash = 0;
    ret->texture_vram_offset = 0;
    ret->palette_vram_offset = 0;
    ret->min_filter = 0xFFFFFFFF;
    ret->mag_filter = 0xFFFFFFFF;
    ret->lod_bias = 0xFFFFFFFF;
    ret->addru = 0xFFFFFFFF;
    ret->addrv = 0xFFFFFFFF;
    ret->addrp = 0xFFFFFFFF;
    ret->border_color_set = false;
    ret->storage_key = storage_key;
    ret->release_texture = recycle_texture_binding;
    ret->release_opaque = r;
    return ret;
}

/* functions for texture LRU cache */
static void texture_cache_entry_init(Lru *lru, LruNode *node, const void *key)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    memcpy(&tnode->key, key, sizeof(TextureKey));

    tnode->binding = NULL;
    tnode->possibly_dirty = false;
}

static void texture_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    if (tnode->binding) {
        pgraph_gl_texture_binding_destroy(tnode->binding);
        tnode->binding = NULL;
        tnode->possibly_dirty = false;
    }
}

static bool texture_cache_entry_compare(Lru *lru, LruNode *node,
                                        const void *key)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    return memcmp(&tnode->key, key, sizeof(TextureKey));
}

void pgraph_gl_init_textures(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    const size_t texture_cache_size = 512;
    r->tex_pool_count = 0;
    r->tex_pool_bytes = 0;
    QTAILQ_INIT(&r->tex_pool_fifo);
    QSLIST_INIT(&r->tex_pool_free);
    for (unsigned int i = 0; i < NV2A_GL_TEX_POOL_SIZE; i++) {
        QSLIST_INSERT_HEAD(&r->tex_pool_free, &r->tex_pool[i], free);
    }
    for (unsigned int i = 0; i < NV2A_GL_TEX_POOL_BUCKETS; i++) {
        QLIST_INIT(&r->tex_pool_buckets[i]);
    }
    memset(&r->tex_pool_stats, 0, sizeof(r->tex_pool_stats));
    const char *log_path = getenv("XEMU_GL_TEXTURE_POOL_LOG");
    r->tex_pool_log = log_path && *log_path ? fopen(log_path, "a") : NULL;
    if (log_path && *log_path && !r->tex_pool_log) {
        error_report("Cannot open GL texture pool log: %s", strerror(errno));
    }
    lru_init(&r->texture_cache);
    r->texture_cache_entries = malloc(texture_cache_size * sizeof(TextureLruNode));
    assert(r->texture_cache_entries != NULL);
    for (int i = 0; i < texture_cache_size; i++) {
        lru_add_free(&r->texture_cache, &r->texture_cache_entries[i].node);
    }

    r->texture_cache.init_node = texture_cache_entry_init;
    r->texture_cache.compare_nodes = texture_cache_entry_compare;
    r->texture_cache.post_node_evict = texture_cache_entry_post_evict;
}

void pgraph_gl_finalize_textures(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (r->texture_binding[i]) {
            pgraph_gl_texture_binding_destroy(r->texture_binding[i]);
        }
        r->texture_binding[i] = NULL;
    }

    lru_flush(&r->texture_cache);

    /*
     * Eviction recycles unreferenced texture objects while the renderer is
     * alive. Drain that pool before the GL context is torn down.
     */
    while (!QTAILQ_EMPTY(&r->tex_pool_fifo)) {
        TexPoolEntry *entry = QTAILQ_FIRST(&r->tex_pool_fifo);
        tex_pool_delete(r, entry->gl_texture);
        tex_pool_remove(r, entry);
    }
    if (r->tex_pool_log) {
        const TexPoolStats *stats = &r->tex_pool_stats;
        fprintf(r->tex_pool_log,
                "{\"schema\":1,\"pool_get_calls\":%" PRIu64
                ",\"pool_hits\":%" PRIu64 ",\"pool_misses\":%" PRIu64
                ",\"pool_entries_scanned\":%" PRIu64
                ",\"pool_puts\":%" PRIu64 ",\"pool_evictions\":%" PRIu64
                ",\"pool_high_water_entries\":%" PRIu64
                ",\"pool_high_water_estimated_bytes\":%" PRIu64
                ",\"gl_texture_generations\":%" PRIu64
                ",\"gl_texture_deletions\":%" PRIu64
                ",\"outgoing_matches_after_miss\":%" PRIu64 "}\n",
                stats->get_calls, stats->hits, stats->misses,
                stats->entries_scanned, stats->puts, stats->evictions,
                stats->high_water_entries, stats->high_water_bytes,
                stats->generations, stats->deletions, stats->outgoing_matches);
        fclose(r->tex_pool_log);
        r->tex_pool_log = NULL;
    }

    free(r->texture_cache_entries);

    r->texture_cache_entries = NULL;
}
