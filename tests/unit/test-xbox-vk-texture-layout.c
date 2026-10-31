/*
 * Production Vulkan decoded cubemap layouts, before staging-buffer upload.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"

NV2AStats g_nv2a_stats;
bool nv2a_vk_text_debug_enabled;
int nv2a_vk_dgroup_indent;
static size_t mapped_size;

void pgraph_vk_text_debug_printf(const char *format, ...)
{
    g_assert_not_reached();
}

void *nv_dma_map(NV2AState *d, hwaddr address, hwaddr *length)
{
    g_assert_cmpuint(address, ==, 0);
    *length = mapped_size;
    return d->vram_ptr;
}

typedef struct FormatCase {
    const char *name;
    unsigned int format, source_bytes, decoded_bytes;
} FormatCase;

static const FormatCase formats[] = {
    { "y8", NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8, 1, 1 },
    { "g8b8", NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8, 2, 2 },
    { "rgb565", NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5, 2, 2 },
    { "rgba8", NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8, 4, 4 },
    { "a8y8", NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8, 2, 2 },
    { "palette", NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8, 1, 4 },
    { "r6g5b5", NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5, 2, 4 },
};

/* Independent endpoints, primaries, interiors and a low-blue input. */
static const struct {
    uint16_t word;
    uint8_t rgba[4];
} known_colors[] = {
    { 0x0000, { 0, 0, 0, 255 } },     { 0xffff, { 255, 255, 255, 255 } },
    { 0xfc00, { 255, 0, 0, 255 } },   { 0x03e0, { 0, 255, 0, 255 } },
    { 0x001f, { 0, 0, 255, 255 } },   { 0x8210, { 130, 132, 132, 255 } },
    { 0x7ceb, { 125, 57, 90, 255 } }, { 0x0001, { 0, 0, 8, 255 } },
};

static unsigned int morton(unsigned int x, unsigned int y)
{
    unsigned int result = 0;
    for (unsigned int bit = 0; bit < 8; bit++) {
        result |= ((x >> bit) & 1) << (2 * bit);
        result |= ((y >> bit) & 1) << (2 * bit + 1);
    }
    return result;
}

static uint32_t palette_color(unsigned int index)
{
    return 0xff000000 | (index << 16) | ((255 - index) << 8) | (index ^ 0x55);
}

static void pixel(const FormatCase *f, unsigned int face, unsigned int level,
                  unsigned int x, unsigned int y, uint8_t *source,
                  uint8_t *expected)
{
    unsigned int seed = (face * 31 + level * 17 + x * 7 + y * 13) % 251 + 1;
    if (f->format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5) {
        seed %= ARRAY_SIZE(known_colors);
        stw_le_p(source, known_colors[seed].word);
        memcpy(expected, known_colors[seed].rgba, 4);
    } else if (f->format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
        *source = seed;
        stl_le_p(expected, palette_color(seed));
    } else {
        for (unsigned int byte = 0; byte < f->source_bytes; byte++) {
            source[byte] = expected[byte] = seed + byte * 23;
        }
    }
}

static void check_layout(const FormatCase *f, unsigned int base_log,
                         bool border, bool clamp_lod)
{
    unsigned int base = 1U << base_log;
    unsigned int levels = base_log + 1;
    unsigned int stored_base = border ? MAX(16U, base * 2) : base;
    size_t face_size = 0;
    for (unsigned int level = 0; level < levels; level++) {
        unsigned int stored = MAX(1U, stored_base >> level);
        face_size += stored * stored * f->source_bytes;
    }
    /* The encoded face alignment is 128 bytes, including unused storage mips.
     */
    size_t stride = (face_size + 127) & ~(size_t)127;
    mapped_size = 4096 + 6 * stride;
    g_autofree uint8_t *vram = g_malloc(mapped_size);
    memset(vram, 0xcd, mapped_size);
    for (unsigned int index = 0; index < 256; index++) {
        stl_le_p(vram + index * 4, palette_color(index));
    }
    for (unsigned int face = 0; face < 6; face++) {
        size_t offset = 4096 + face * stride;
        for (unsigned int level = 0; level < levels; level++) {
            unsigned int stored = MAX(1U, stored_base >> level);
            unsigned int logical = MAX(1U, base >> level);
            unsigned int inset = border && level < 3 ? 4U >> level : 0;
            inset = MIN(inset, stored - logical);
            for (unsigned int y = 0; y < logical; y++) {
                for (unsigned int x = 0; x < logical; x++) {
                    uint8_t unused[4];
                    pixel(f, face, level, x, y,
                          vram + offset +
                              morton(x + inset, y + inset) * f->source_bytes,
                          unused);
                }
            }
            offset += stored * stored * f->source_bytes;
        }
    }

    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    d->vram_ptr = vram;
    unsigned int visible_levels = clamp_lod ? 1 : levels;
    uint32_t format = 0, control = 0;
    SET_MASK(format, NV_PGRAPH_TEXFMT0_CUBEMAPENABLE, 1);
    SET_MASK(format, NV_PGRAPH_TEXFMT0_DIMENSIONALITY, 2);
    SET_MASK(format, NV_PGRAPH_TEXFMT0_COLOR, f->format);
    SET_MASK(format, NV_PGRAPH_TEXFMT0_BASE_SIZE_U, base_log);
    SET_MASK(format, NV_PGRAPH_TEXFMT0_BASE_SIZE_V, base_log);
    SET_MASK(format, NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS, levels);
    SET_MASK(format, NV_PGRAPH_TEXFMT0_BORDER_SOURCE,
             border ? 0 : NV_PGRAPH_TEXFMT0_BORDER_SOURCE_COLOR);
    SET_MASK(control, NV_PGRAPH_TEXCTL0_0_MAX_LOD_CLAMP, visible_levels - 1);
    pgraph_reg_w(&d->pgraph, NV_PGRAPH_TEXFMT0, format);
    pgraph_reg_w(&d->pgraph, NV_PGRAPH_TEXCTL0_0, control);
    pgraph_reg_w(&d->pgraph, NV_PGRAPH_TEXOFFSET0, 4096);
    g_autoptr(TextureLayout) layout = get_texture_layout(&d->pgraph, 0, false);
    for (unsigned int face = 0; face < 6; face++) {
        for (unsigned int level = 0; level < visible_levels; level++) {
            TextureLevel *mip = &layout->layers[face].levels[level];
            unsigned int logical = MAX(1U, base >> level);
            g_assert_cmpuint(mip->width, ==, logical);
            g_assert_cmpuint(mip->height, ==, logical);
            g_assert_cmpuint(mip->depth, ==, 1);
            g_assert_cmpuint(mip->decoded_size, ==,
                             logical * logical * f->decoded_bytes);
            g_assert_true(mip->owns_data);
            for (unsigned int y = 0; y < logical; y++) {
                for (unsigned int x = 0; x < logical; x++) {
                    uint8_t source[4], expected[4];
                    pixel(f, face, level, x, y, source, expected);
                    const uint8_t *actual = mip->decoded_data;
                    actual += (y * logical + x) * f->decoded_bytes;
                    g_assert_cmpmem(actual, f->decoded_bytes, expected,
                                    f->decoded_bytes);
                }
            }
        }
    }
}

static void test_bordered(gconstpointer data)
{
    for (unsigned int log = 0; log <= 4; log++) {
        check_layout(data, log, true, false);
        check_layout(data, log, true, true);
    }
}

static void test_borderless(void)
{
    for (unsigned int format = 0; format < ARRAY_SIZE(formats); format++) {
        for (unsigned int log = 0; log <= 3; log++) {
            check_layout(&formats[format], log, false, false);
        }
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    for (unsigned int i = 0; i < ARRAY_SIZE(formats); i++) {
        g_autofree char *name = g_strdup_printf(
            "/xbox/vk/texture-layout/bordered/%s", formats[i].name);
        g_test_add_data_func(name, &formats[i], test_bordered);
    }
    g_test_add_func("/xbox/vk/texture-layout/borderless-control",
                    test_borderless);
    return g_test_run();
}
