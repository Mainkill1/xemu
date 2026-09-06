/*
 * NV2A Vulkan completed display conversion reuse helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_DISPLAY_REUSE_H
#define HW_XBOX_NV2A_PGRAPH_VK_DISPLAY_REUSE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PGRAPHVkDisplayReuseKey {
    bool valid;
    uint64_t surface_lifetime_id;
    int surface_draw_time;
    int guest_frame_time;
    uint64_t scanout_address;
    uint32_t vga_line_offset;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t surface_scale_factor;
    uint8_t interlace_mode;
} PGRAPHVkDisplayReuseKey;

static inline void pgraph_vk_display_reuse_reset(PGRAPHVkDisplayReuseKey *key)
{
    key->valid = false;
}

static inline bool pgraph_vk_display_reuse_allowed(bool tcg,
                                                   bool pvideo_enabled,
                                                   bool upload_pending,
                                                   bool image_recreated)
{
    return tcg && !pvideo_enabled && !upload_pending && !image_recreated;
}

static inline bool pgraph_vk_display_reuse_key_matches(
    const PGRAPHVkDisplayReuseKey *key, uint64_t surface_lifetime_id,
    int surface_draw_time, int guest_frame_time, uint64_t scanout_address,
    uint32_t vga_line_offset, uint32_t width, uint32_t height,
    uint32_t surface_scale_factor, uint8_t interlace_mode)
{
    return key->valid &&
           key->surface_lifetime_id == surface_lifetime_id &&
           key->surface_draw_time == surface_draw_time &&
           key->guest_frame_time == guest_frame_time &&
           key->scanout_address == scanout_address &&
           key->vga_line_offset == vga_line_offset &&
           key->display_width == width && key->display_height == height &&
           key->surface_scale_factor == surface_scale_factor &&
           key->interlace_mode == interlace_mode;
}

static inline void pgraph_vk_display_reuse_publish(
    PGRAPHVkDisplayReuseKey *key, uint64_t surface_lifetime_id,
    int surface_draw_time, int guest_frame_time, uint64_t scanout_address,
    uint32_t vga_line_offset, uint32_t width, uint32_t height,
    uint32_t surface_scale_factor, uint8_t interlace_mode)
{
    key->surface_lifetime_id = surface_lifetime_id;
    key->surface_draw_time = surface_draw_time;
    key->guest_frame_time = guest_frame_time;
    key->scanout_address = scanout_address;
    key->vga_line_offset = vga_line_offset;
    key->display_width = width;
    key->display_height = height;
    key->surface_scale_factor = surface_scale_factor;
    key->interlace_mode = interlace_mode;
    key->valid = true;
}

#endif
