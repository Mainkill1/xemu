/*
 * NV2A Vulkan draw completion lifecycle
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "draw-lifecycle.h"
#include "renderer.h"

PGRAPHVkDrawShaderMissAction pgraph_vk_draw_shader_miss_action(
    bool continue_requested, bool nonblocking_supported,
    bool omission_supported, bool executable_ready)
{
    if (executable_ready) {
        return PGRAPH_VK_DRAW_MISS_READY;
    }
    if (continue_requested && nonblocking_supported && omission_supported) {
        return PGRAPH_VK_DRAW_MISS_OMIT;
    }
    return PGRAPH_VK_DRAW_MISS_WAIT;
}

void pgraph_vk_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHVkState *r, PGRAPHVkDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty)
{
    if (result != PGRAPH_VK_DRAW_SUBMITTED) {
        return;
    }

    pg->draw_time++;
    if (r->color_binding && color_write) {
        r->color_binding->draw_time = pg->draw_time;
    }
    if (r->zeta_binding && zeta_write) {
        r->zeta_binding->draw_time = pg->draw_time;
    }
    pgraph_vk_set_surface_dirty(pg, color_dirty, zeta_dirty);
}
