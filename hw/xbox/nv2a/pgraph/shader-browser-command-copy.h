/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_COMMAND_COPY_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_COMMAND_COPY_H

#include <stddef.h>
#include <stdbool.h>

/* A method owns its active packet. DRAW_ARRAYS may additionally inspect three
 * following method/parameter pairs, ending at word six. Remaining FIFO data is
 * unrelated to this event and must not be copied for every method. */
static inline size_t xemu_shader_capture_command_words(size_t packet_words,
                                                       size_t lookahead_words,
                                                       bool draw_arrays)
{
    size_t inspected =
        draw_arrays ? (lookahead_words < 7 ? lookahead_words : 7) : 0;
    return packet_words > inspected ? packet_words : inspected;
}

#endif
