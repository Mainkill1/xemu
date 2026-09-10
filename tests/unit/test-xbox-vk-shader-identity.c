/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdint.h>

#include "hw/xbox/nv2a/pgraph/vk/shader-identity.h"

static void test_exact_source_classification(void)
{
    PGRAPHVkShaderIdentityTracker tracker;
    static const char first_source[] = "void main() { gl_Position.x = 1; }";
    static const char colliding_source[] = "void main() { gl_Position.x = 2; }";
    const uint64_t forced_hash = UINT64_C(0x123456789abcdef0);

    assert(pgraph_vk_shader_identity_tracker_init(&tracker, 4, 4, 1024));
    assert(pgraph_vk_shader_identity_observe(
               &tracker, 1, first_source, sizeof(first_source) - 1,
               forced_hash, 11, 77, 7, 101, 201, 301) ==
           PGRAPH_VK_SHADER_SOURCE_FIRST);
    assert(pgraph_vk_shader_identity_observe(
               &tracker, 1, first_source, sizeof(first_source) - 1,
               forced_hash, 12, 78, 8, 102, 202, 302) ==
           PGRAPH_VK_SHADER_SOURCE_REPEAT);

    /* Hash equality alone must not classify different bytes as a repeat. */
    assert(pgraph_vk_shader_identity_observe(
               &tracker, 1, colliding_source,
               sizeof(colliding_source) - 1, forced_hash, 13, 79, 9, 103,
               203, 303) ==
           PGRAPH_VK_SHADER_SOURCE_FIRST);

    /* Stage is part of generated shader identity. */
    assert(pgraph_vk_shader_identity_observe(
               &tracker, 16, first_source, sizeof(first_source) - 1,
               forced_hash, 14, 80, 10, 104, 204, 304) ==
           PGRAPH_VK_SHADER_SOURCE_FIRST);

    size_t record_count = 0;
    const PGRAPHVkShaderIdentityRecord *records =
        pgraph_vk_shader_identity_records(&tracker, &record_count);
    assert(record_count == 4);
    assert(records[0].key_hash == 11 && records[0].generation_us == 7 &&
           records[0].compile_us == 101 &&
           records[0].module_create_us == 201 &&
           records[0].reflection_us == 301 &&
           records[0].profile_frame == 77 &&
           records[0].source_class == PGRAPH_VK_SHADER_SOURCE_FIRST);
    assert(records[1].key_hash == 12 &&
           records[1].source_class == PGRAPH_VK_SHADER_SOURCE_REPEAT);
    assert(!pgraph_vk_shader_identity_records_saturated(&tracker));
    pgraph_vk_shader_identity_tracker_destroy(&tracker);
}

static void test_bounds_do_not_forget_tracked_sources(void)
{
    PGRAPHVkShaderIdentityTracker tracker;
    static const char tracked[] = "tracked";
    static const char overflow[] = "overflow";

    assert(pgraph_vk_shader_identity_tracker_init(
        &tracker, 1, 2, sizeof(tracked) - 1));
    assert(pgraph_vk_shader_identity_observe(
               &tracker, 1, tracked, sizeof(tracked) - 1, 1, 1, 1, 1, 1, 1,
               1) ==
           PGRAPH_VK_SHADER_SOURCE_FIRST);
    assert(pgraph_vk_shader_identity_observe(
               &tracker, 1, overflow, sizeof(overflow) - 1, 2, 2, 2, 2, 2, 2,
               2) ==
           PGRAPH_VK_SHADER_SOURCE_SATURATED);
    assert(pgraph_vk_shader_identity_observe(
               &tracker, 1, tracked, sizeof(tracked) - 1, 1, 3, 3, 3, 3, 3,
               3) ==
           PGRAPH_VK_SHADER_SOURCE_REPEAT);
    assert(pgraph_vk_shader_identity_records_saturated(&tracker));
    pgraph_vk_shader_identity_tracker_destroy(&tracker);
}

static void test_disabled_tracker_does_not_record(void)
{
    PGRAPHVkShaderIdentityTracker tracker = { 0 };
    static const char source[] = "source";
    size_t record_count = 99;

    assert(pgraph_vk_shader_identity_observe(
               &tracker, 1, source, sizeof(source) - 1, 1, 2, 3, 4, 5, 6,
               7) ==
           PGRAPH_VK_SHADER_SOURCE_SATURATED);
    assert(!pgraph_vk_shader_identity_records(&tracker, &record_count));
    assert(record_count == 0);
}

int main(void)
{
    test_exact_source_classification();
    test_bounds_do_not_forget_tracked_sources();
    test_disabled_tracker_does_not_record();
    return 0;
}
