/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#ifndef TOPOLOGY_COVERAGE
#define TOPOLOGY_COVERAGE 0
#endif

#define TILE_COUNT 32
#define VERTICES_PER_TILE 6
#define GPU_ADDRESS(pointer) ((uint32_t)(uintptr_t)(pointer) & 0x03ffffff)

typedef struct CaptureVertex {
    float position[4];
    float tint[4];
    float uv[2];
} CaptureVertex;

static CaptureVertex *vertices;
static uint32_t *textures[2];

#if TOPOLOGY_COVERAGE
static unsigned tile_primitive(unsigned tile)
{
    static const unsigned modes[6] = {
        NV097_SET_BEGIN_END_OP_TRIANGLES,
        NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP,
        NV097_SET_BEGIN_END_OP_TRIANGLE_FAN,
        NV097_SET_BEGIN_END_OP_QUADS,
        NV097_SET_BEGIN_END_OP_QUAD_STRIP,
        NV097_SET_BEGIN_END_OP_POLYGON,
    };
    return modes[tile % 6];
}
#endif

static uint32_t field(uint32_t mask, uint32_t value)
{
    return (value << __builtin_ctz(mask)) & mask;
}

static void make_inputs(void)
{
    vertices = MmAllocateContiguousMemoryEx(
        sizeof(*vertices) * TILE_COUNT * VERTICES_PER_TILE, 0, 0x03ffafff, 0,
        PAGE_READWRITE | PAGE_WRITECOMBINE);
    for (unsigned slot = 0; slot < 2; ++slot)
        textures[slot] = MmAllocateContiguousMemoryEx(
            64, 0, 0x03ffafff, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!vertices || !textures[0] || !textures[1]) {
        debugPrint("Capture fixture: input allocation failed\n");
        for (;;)
            Sleep(1000);
    }
    const unsigned corners[6] = { 0, 1, 2, 0, 2, 3 };
#if TOPOLOGY_COVERAGE
    const unsigned perimeter[6] = { 0, 1, 2, 3, 0, 0 };
    const unsigned strip[6] = { 0, 1, 3, 2, 0, 0 };
#endif
    for (unsigned tile = 0; tile < TILE_COUNT; ++tile) {
#if TOPOLOGY_COVERAGE
        const unsigned primitive = tile_primitive(tile);
        const unsigned *tile_corners =
            primitive == NV097_SET_BEGIN_END_OP_TRIANGLES ? corners :
            primitive == NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP ||
                    primitive == NV097_SET_BEGIN_END_OP_QUAD_STRIP ?
                                                            strip :
                                                            perimeter;
#endif
        const float half_x = 23.0f + (tile % 5);
        const float half_y = 28.0f + (tile % 7);
        for (unsigned i = 0; i < VERTICES_PER_TILE; ++i) {
            CaptureVertex *vertex = &vertices[tile * VERTICES_PER_TILE + i];
#if TOPOLOGY_COVERAGE
            const unsigned corner = tile_corners[i];
#else
            const unsigned corner = corners[i];
#endif
            vertex->position[0] = corner == 0 || corner == 3 ? -half_x : half_x;
            vertex->position[1] = corner < 2 ? -half_y : half_y;
            vertex->position[2] = 0.5f;
            vertex->position[3] = 1.0f;
            vertex->tint[0] = 0.55f + (tile % 8) * 0.05f;
            vertex->tint[1] = 0.6f + (tile / 8) * 0.08f;
            vertex->tint[2] = 1.0f;
            vertex->tint[3] = 1.0f;
            vertex->uv[0] = corner == 0 || corner == 3 ? 0.0f : 1.0f;
            vertex->uv[1] = corner < 2 ? 0.0f : 1.0f;
        }
    }
    // Four-by-four Morton layout; these are independent authored patterns.
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            unsigned address =
                (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2);
            textures[0][address] = ((x ^ y) & 1) ? 0xffd8e8ff : 0xff4088c0;
            textures[1][address] = ((x + y) & 1) ? 0xffffc878 : 0xff905030;
        }
}

static void install_pipeline(void)
{
    static const uint32_t program[] = {
#include "capture-vertex.inl"
    };
    uint32_t *p = pb_begin();
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);
    p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
                 field(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE,
                       NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM) |
                     field(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE,
                           NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    pb_end(p);
    for (unsigned instruction = 0; instruction < sizeof(program) / 16;
         ++instruction) {
        p = pb_begin();
        pb_push(p++, NV097_SET_TRANSFORM_PROGRAM, 4);
        memcpy(p, program + instruction * 4, 16);
        pb_end(p + 4);
    }
    p = pb_begin();
#include "capture-pixel.inl"
    p = pb_push1(p, NV097_SET_BLEND_ENABLE, 0);
    p = pb_push1(p, NV097_SET_ALPHA_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_CULL_FACE_ENABLE, 0);
    p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_DEPTH_MASK, 0);
    p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_COLOR_MASK, 0x01010101);
    for (unsigned attribute = 0; attribute < 16; ++attribute)
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 4 * attribute,
                     NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    const unsigned slots[] = { 0, 3, 9 };
    const unsigned components[] = { 4, 4, 2 };
    const unsigned offsets[] = { 0, 16, 32 };
    for (unsigned i = 0; i < 3; ++i) {
        p = pb_push1(
            p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 4 * slots[i],
            field(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE,
                  NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F) |
                field(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, components[i]) |
                field(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE,
                      sizeof(*vertices)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 4 * slots[i],
                     GPU_ADDRESS(vertices) + offsets[i]);
    }
    p = pb_push1(p, NV097_SET_TEXTURE_FORMAT,
                 field(NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA, 1) |
                     field(NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE,
                           NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE_COLOR) |
                     field(NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY, 2) |
                     field(NV097_SET_TEXTURE_FORMAT_COLOR,
                           NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8) |
                     field(NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS, 1) |
                     field(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U, 2) |
                     field(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V, 2));
    // Clamp-to-edge (5), box minification (1), and nearest magnification (1)
    // agree with xemu's NV_PGRAPH_TEXADDRESS/TEXFILTER definitions.
    p = pb_push1(p, NV097_SET_TEXTURE_ADDRESS,
                 field(NV097_SET_TEXTURE_ADDRESS_U, 5) |
                     field(NV097_SET_TEXTURE_ADDRESS_V, 5) |
                     field(NV097_SET_TEXTURE_ADDRESS_P, 5));
    p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0,
                 NV097_SET_TEXTURE_CONTROL0_ENABLE);
    p = pb_push1(p, NV097_SET_TEXTURE_FILTER,
                 field(NV097_SET_TEXTURE_FILTER_MIN, 1) |
                     field(NV097_SET_TEXTURE_FILTER_MAG, 1));
    p = pb_push1(p, NV097_SET_TEXTURE_IMAGE_RECT, (4U << 16) | 4);
    for (unsigned slot = 1; slot < 4; ++slot)
        p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0 + 64 * slot, 0);
    pb_end(p);
}

static void draw_tile(unsigned tile, unsigned frame)
{
    uint32_t *p = pb_begin();
    p = pb_push1(p, NV097_SET_TEXTURE_OFFSET, GPU_ADDRESS(textures[tile & 1]));
    p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT_LOAD, 96);
    const int wave = (int)(frame % 120) - 60;
    const float displacement =
        tile == 17 ? (wave < 0 ? -wave : wave) * 0.15f : 0;
    p = pb_push4f(p, NV097_SET_TRANSFORM_CONSTANT,
                  40.0f + (tile % 8) * 80 + displacement,
                  60.0f + (tile / 8) * 120, 0, 0);
    const unsigned shade = 144 + tile * 3;
    const bool defect = tile == 17 && frame % 240 >= 120 && frame % 240 < 150;
    const uint32_t factor =
        defect ? 0xff000000 :
                 0xff000000 | (shade << 16) | ((255 - tile * 2) << 8) | 224;
    p = pb_push1(p, NV097_SET_COMBINER_FACTOR0, factor);
#if TOPOLOGY_COVERAGE
    const unsigned primitive = tile_primitive(tile);
    const unsigned vertex_count =
        primitive == NV097_SET_BEGIN_END_OP_TRIANGLES ? VERTICES_PER_TILE : 4;
    p = pb_push1(p, NV097_SET_BEGIN_END, primitive);
#else
    p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
#endif
    p = pb_push1(
        p, NV097_DRAW_ARRAYS | 0x40000000,
        field(NV097_DRAW_ARRAYS_START_INDEX, tile * VERTICES_PER_TILE) |
#if TOPOLOGY_COVERAGE
            field(NV097_DRAW_ARRAYS_COUNT, vertex_count - 1));
#else
            field(NV097_DRAW_ARRAYS_COUNT, VERTICES_PER_TILE - 1));
#endif
    p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    pb_end(p);
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    if (pb_init()) {
        debugPrint("Capture fixture: pbkit initialization failed\n");
        Sleep(2000);
        return 1;
    }
    make_inputs();
    pb_show_front_screen();
    install_pipeline();
    for (unsigned frame = 0;; ++frame) {
        pb_wait_for_vbl();
        pb_reset();
        pb_target_back_buffer();
        pb_erase_depth_stencil_buffer(0, 0, 640, 480);
        pb_fill(0, 0, 640, 480, 0xff182028);
        while (pb_busy()) {
        }
        for (unsigned tile = 0; tile < TILE_COUNT; ++tile)
            draw_tile(tile, frame);
        while (pb_busy()) {
        }
        while (pb_finished()) {
        }
    }
}
