/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/vk/display-reuse.h"

static PGRAPHVkDisplayReuseKey published_key(void)
{
    PGRAPHVkDisplayReuseKey key = { 0 };

    pgraph_vk_display_reuse_publish(&key, 11, 12, 13, 14, 15, 640, 480, 2,
                                    3);
    return key;
}

static bool test_positive_match(void)
{
    PGRAPHVkDisplayReuseKey key = published_key();

    return pgraph_vk_display_reuse_key_matches(&key, 11, 12, 13, 14, 15,
                                               640, 480, 2, 3);
}

static bool test_every_key_field_invalidates(void)
{
    PGRAPHVkDisplayReuseKey key = published_key();

#define MUST_MISS(...)                                                        \
    do {                                                                      \
        if (pgraph_vk_display_reuse_key_matches(&key, __VA_ARGS__)) {         \
            return false;                                                     \
        }                                                                     \
    } while (0)

    MUST_MISS(10, 12, 13, 14, 15, 640, 480, 2, 3);
    MUST_MISS(11, 10, 13, 14, 15, 640, 480, 2, 3);
    MUST_MISS(11, 12, 10, 14, 15, 640, 480, 2, 3);
    MUST_MISS(11, 12, 13, 10, 15, 640, 480, 2, 3);
    MUST_MISS(11, 12, 13, 14, 10, 640, 480, 2, 3);
    MUST_MISS(11, 12, 13, 14, 15, 320, 480, 2, 3);
    MUST_MISS(11, 12, 13, 14, 15, 640, 240, 2, 3);
    MUST_MISS(11, 12, 13, 14, 15, 640, 480, 1, 3);
    MUST_MISS(11, 12, 13, 14, 15, 640, 480, 2, 1);
#undef MUST_MISS
    return true;
}

static bool test_runtime_exclusions(void)
{
    return pgraph_vk_display_reuse_allowed(true, false, false, false) &&
           !pgraph_vk_display_reuse_allowed(false, false, false, false) &&
           !pgraph_vk_display_reuse_allowed(true, true, false, false) &&
           !pgraph_vk_display_reuse_allowed(true, false, true, false) &&
           !pgraph_vk_display_reuse_allowed(true, false, false, true);
}

static bool test_explicit_reset(void)
{
    PGRAPHVkDisplayReuseKey key = published_key();

    pgraph_vk_display_reuse_reset(&key);
    return !pgraph_vk_display_reuse_key_matches(&key, 11, 12, 13, 14, 15,
                                                640, 480, 2, 3);
}

int main(void)
{
    bool positive = test_positive_match();
    bool fields = test_every_key_field_invalidates();
    bool exclusions = test_runtime_exclusions();
    bool reset = test_explicit_reset();

    puts("TAP version 13");
    puts("1..4");
    printf("%s 1 - identical completed display matches\n",
           positive ? "ok" : "not ok");
    printf("%s 2 - every display key field prevents stale reuse\n",
           fields ? "ok" : "not ok");
    printf("%s 3 - runtime exclusions prevent reuse\n",
           exclusions ? "ok" : "not ok");
    printf("%s 4 - explicit invalidation prevents reuse\n",
           reset ? "ok" : "not ok");

    return positive && fields && exclusions && reset ? 0 : 1;
}
