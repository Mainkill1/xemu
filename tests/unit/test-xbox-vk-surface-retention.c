/*
 * Vulkan retained readback ownership tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/surface-retention.h"

typedef struct Readback {
    uint8_t guest[4];
    bool fail;
} Readback;

static bool read_image(void *opaque, uint8_t *destination)
{
    Readback *readback = opaque;
    static const uint8_t image[4] = { 1, 2, 3, 4 };

    /* The guest changes RAM after the image copy, before publication. */
    memcpy(destination, image, sizeof(image));
    memset(readback->guest, 9, sizeof(readback->guest));
    return !readback->fail;
}

static void test_owned_image_bytes(void)
{
    Readback readback = { .guest = { 5, 6, 7, 8 } };
    uint8_t snapshot[4];
    static const uint8_t image[4] = { 1, 2, 3, 4 };

    g_assert_true(pgraph_vk_surface_readback_owned(
        readback.guest, snapshot, sizeof(snapshot), read_image, &readback));
    g_assert_cmpmem(snapshot, sizeof(snapshot), image, sizeof(image));
    /* Later writes cannot poison the retained image reference. */
    memset(readback.guest, 7, sizeof(readback.guest));
    g_assert_cmpmem(snapshot, sizeof(snapshot), image, sizeof(image));
}

static void test_failed_readback_not_published(void)
{
    Readback readback = { .guest = { 5, 6, 7, 8 }, .fail = true };
    uint8_t snapshot[4];
    static const uint8_t guest_write[4] = { 9, 9, 9, 9 };

    g_assert_false(pgraph_vk_surface_readback_owned(
        readback.guest, snapshot, sizeof(snapshot), read_image, &readback));
    g_assert_cmpmem(readback.guest, sizeof(readback.guest), guest_write,
                    sizeof(guest_write));
}

static void test_padding_preserved(void)
{
    Readback readback = { .guest = { 5, 6, 7, 8 } };
    uint8_t snapshot[8];
    uint8_t guest[8] = { 0, 0, 0, 0, 11, 12, 13, 14 };
    static const uint8_t expected[8] = { 1, 2, 3, 4, 11, 12, 13, 14 };

    g_assert_true(pgraph_vk_surface_readback_owned(
        guest, snapshot, sizeof(snapshot), read_image, &readback));
    g_assert_cmpmem(snapshot, sizeof(snapshot), expected, sizeof(expected));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/retention/owned-image", test_owned_image_bytes);
    g_test_add_func("/xbox/vk/retention/failed-readback",
                    test_failed_readback_not_published);
    g_test_add_func("/xbox/vk/retention/padding", test_padding_preserved);
    return g_test_run();
}
