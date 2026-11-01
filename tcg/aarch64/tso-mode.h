/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TCG_AARCH64_TSO_MODE_H
#define TCG_AARCH64_TSO_MODE_H

#include <stdbool.h>
#include <string.h>

/* Explicit opt-in only. Invalid values and unknown capabilities fail closed. */
static inline bool tcg_aarch64_tso_mode(const char *value, bool have_lrcpc)
{
    return have_lrcpc && value &&
           (!strcmp(value, "1") || !strcmp(value, "on") ||
            !strcmp(value, "true") || !strcmp(value, "yes"));
}

#endif
