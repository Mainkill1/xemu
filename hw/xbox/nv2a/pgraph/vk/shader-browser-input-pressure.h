/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_SHADER_BROWSER_INPUT_PRESSURE_H
#define HW_XBOX_NV2A_PGRAPH_VK_SHADER_BROWSER_INPUT_PRESSURE_H

#include <stdbool.h>
#include <stddef.h>

#define PGRAPH_VK_INPUT_STAGING_BUDGET (256U * 1024U * 1024U)

static inline bool pgraph_vk_input_should_drain(size_t staged_bytes,
                                                bool in_command_buffer,
                                                bool in_draw,
                                                unsigned debug_depth)
{
    /* Leave room for a bounded draw's copies before the next safe boundary.
     * Finishing submits the recorded copies and retires them after the fence;
     * it must never run inside a draw or an open debug marker. */
    return staged_bytes >= PGRAPH_VK_INPUT_STAGING_BUDGET / 2 &&
           in_command_buffer && !in_draw && !debug_depth;
}

#endif
