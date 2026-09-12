/*
 * NV2A Vulkan texture-binding transition tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
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

int main(void)
{
    bool disabled = test_disabled_stage_keeps_dirty_for_reenable();
    bool retry = test_failed_active_bind_remains_retryable();

    puts("TAP version 13");
    puts("1..2");
    printf("%s 1 - disabled dirty state waits for re-enable\n",
           disabled ? "ok" : "not ok");
    printf("%s 2 - failed active bind remains retryable\n",
           retry ? "ok" : "not ok");
    return disabled && retry ? 0 : 1;
}
