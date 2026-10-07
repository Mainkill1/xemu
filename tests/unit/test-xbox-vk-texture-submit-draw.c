/*
 * Expose the actual descriptor-binding boundary to the lifetime fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#define pgraph_vk_finish texture_submit_unused_finish
#include "hw/xbox/nv2a/pgraph/vk/draw.c"
#undef pgraph_vk_finish

#include "test-xbox-vk-texture-submit.h"

void test_texture_submit_bind(PGRAPHState *pg)
{
    bind_descriptor_sets(pg);
}
