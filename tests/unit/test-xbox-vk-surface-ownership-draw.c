/*
 * Keep excluded GPU boundaries distinct from the fixture's direct hooks.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#define pgraph_vk_finish surface_ownership_unused_finish
#define pgraph_vk_ensure_not_in_render_pass \
    surface_ownership_unused_ensure_not_in_render_pass
#include "hw/xbox/nv2a/pgraph/vk/draw.c"
#undef pgraph_vk_ensure_not_in_render_pass
#undef pgraph_vk_finish
