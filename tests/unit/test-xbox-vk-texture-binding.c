/*
 * NV2A Vulkan texture-binding transition tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/vk/texture-binding-state.h"

static bool test_disabled_stage_keeps_dirty_for_reenable(void)
{
    bool enabled = false;
    bool dirty = true;
    bool bound = false;
    bool dummy = false;

    if (!pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }

    /* The first disabled bind selects the dummy without retiring guest state. */
    bound = true;
    dummy = true;
    if (!pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy) ||
        !dirty) {
        return false;
    }

    enabled = true;
    if (!pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }
    dummy = false;
    dirty = false;
    if (pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }

    enabled = false;
    if (!pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }
    dummy = true;
    return !pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy);
}

static bool test_failed_active_bind_remains_retryable(void)
{
    /* A failed bind selects the dummy and retains dirtiness. */
    return pgraph_vk_texture_stage_needs_rebind(true, true, true, true) &&
           pgraph_vk_texture_stage_needs_rebind(true, false, true, true);
}

static bool test_texture_source_identity(void)
{
    const uint64_t texture = 0x1000;
    const uint64_t palette = 0x2000;

    return pgraph_vk_texture_source_identity_matches(
               false, true, false, false,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, false, false, false,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, true, false,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, false, true,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture, texture, 0, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture, texture, 32, palette, palette) &&
           !pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture + 1, texture, 0, palette, palette) &&
           !pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture, texture, 32, palette + 1, palette);
}

int main(void)
{
    bool disabled = test_disabled_stage_keeps_dirty_for_reenable();
    bool retry = test_failed_active_bind_remains_retryable();
    bool identity = test_texture_source_identity();

    puts("TAP version 13");
    puts("1..3");
    printf("%s 1 - disabled dirty state waits for re-enable\n",
           disabled ? "ok" : "not ok");
    printf("%s 2 - failed active bind remains retryable\n",
           retry ? "ok" : "not ok");
    printf("%s 3 - texture source identity gates clean reuse\n",
           identity ? "ok" : "not ok");
    return disabled && retry && identity ? 0 : 1;
}
