/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/draw.c"
#include "test-xbox-vk-texture-submit.h"

void test_texture_submit_bind(PGRAPHState *pg)
{
    bind_descriptor_sets(pg);
}
