/*
 * NV2A Vulkan graphics pipeline cache lifetime helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_PIPELINE_CACHE_LIFETIME_H
#define HW_XBOX_NV2A_PGRAPH_VK_PIPELINE_CACHE_LIFETIME_H

#include <stdbool.h>

static inline bool pgraph_vk_pipeline_cache_entry_can_evict(
    bool in_command_buffer, unsigned int entry_draw_time,
    unsigned int command_buffer_start_time)
{
    return !in_command_buffer || entry_draw_time < command_buffer_start_time;
}

#endif
