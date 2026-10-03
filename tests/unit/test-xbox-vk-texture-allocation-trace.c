/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/xbox/nv2a/pgraph/vk/texture-allocation-trace.h"

static const VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .extent = { 17, 19, 1 },
    .mipLevels = 4,
    .arrayLayers = 6,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
};
static const VmaAllocationCreateInfo alloc_info = {
    .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .requiredFlags = 3,
    .preferredFlags = 5,
    .memoryTypeBits = 7,
    .priority = 0.5f,
    .minAlignment = 256,
};
static VmaAllocator allocator = (VmaAllocator)(uintptr_t)0x100;
static VkResult next_result;
static unsigned creates, destroys, queries;
static const VkImageCreateInfo *expected_image_info;
static const VmaAllocationCreateInfo *expected_alloc_info;
static VkImage created_image;
static VmaAllocation created_allocation;

/* Replace the driver boundary, preserving the production wrapper and writer. */
VkResult vmaCreateImage(VmaAllocator a, const VkImageCreateInfo *i,
                        const VmaAllocationCreateInfo *c, VkImage *image,
                        VmaAllocation *allocation, VmaAllocationInfo *info)
{
    g_assert_true(a == allocator);
    g_assert_true(i == expected_image_info);
    g_assert_true(c == expected_alloc_info);
    g_assert_null(info);
    creates++;
    if (next_result == VK_SUCCESS) {
        created_image = (VkImage)(uintptr_t)(0x1000 + creates);
        created_allocation = (VmaAllocation)(uintptr_t)(0x2000 + creates);
        *image = created_image;
        *allocation = created_allocation;
    }
    return next_result;
}

void vmaGetAllocationInfo(VmaAllocator a, VmaAllocation allocation,
                          VmaAllocationInfo *info)
{
    g_assert_true(a == allocator);
    g_assert_true(allocation == created_allocation);
    queries++;
    *info = (VmaAllocationInfo){ .size = 123456, .memoryType = 2 };
}

void vmaDestroyImage(VmaAllocator a, VkImage image, VmaAllocation allocation)
{
    g_assert_true(a == allocator);
    g_assert_true(image == created_image);
    g_assert_true(allocation == created_allocation);
    destroys++;
}

static void reset(void)
{
    creates = destroys = queries = 0;
    created_image = VK_NULL_HANDLE;
    created_allocation = NULL;
    next_result = VK_SUCCESS;
    expected_image_info = &image_info;
    expected_alloc_info = &alloc_info;
}

static void create(PGRAPHVkTextureAllocationTrace *trace, bool surface_copy)
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = NULL;
    VkResult result = pgraph_vk_texture_image_create(
        trace, allocator, expected_image_info, expected_alloc_info, &image,
        &allocation, surface_copy, 9);
    g_assert_cmpint(result, ==, next_result);
    if (result == VK_SUCCESS) {
        g_assert_true(image == created_image);
        g_assert_true(allocation == created_allocation);
    } else {
        g_assert_true(image == VK_NULL_HANDLE);
        g_assert_null(allocation);
    }
}

static char *new_path(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("texture-allocation-test-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "trace.jsonl", NULL);
    g_free(dir);
    return path;
}

static char *read_and_remove(char *path)
{
    char *contents = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_unlink(path), ==, 0);
    char *dir = g_path_get_dirname(path);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    g_free(dir);
    g_free(path);
    return contents;
}

static void has(const char *output, const char *field)
{
    g_assert_nonnull(strstr(output, field));
}

static void test_disabled(void)
{
    reset();
    g_assert_null(pgraph_vk_texture_allocation_trace_open(NULL, 10));
    g_assert_null(pgraph_vk_texture_allocation_trace_open("", 10));
    create(NULL, false);
    pgraph_vk_texture_image_destroy(NULL, allocator, created_image,
                                    created_allocation, 10);
    g_assert_cmpuint(creates, ==, 1);
    g_assert_cmpuint(destroys, ==, 1);
    g_assert_cmpuint(queries, ==, 0);
    g_assert_true(pgraph_vk_texture_allocation_trace_close(NULL));
}

static void test_actual_configuration(void)
{
    reset();
    char *path = new_path();
    PGRAPHVkTextureAllocationTrace *trace =
        pgraph_vk_texture_allocation_trace_open(path, 10);
    g_assert_nonnull(trace);
    pgraph_vk_texture_allocation_trace_frame(trace);
    create(trace, false);
    g_assert_cmpuint(creates, ==, 1);
    g_assert_cmpuint(queries, ==, 1);
    g_assert_true(pgraph_vk_texture_allocation_trace_close(trace));
    char *output = read_and_remove(path);
    const char *fields[] = {
        "\"schema_version\":2",
        "\"duration_unit\":\"host_elapsed_us\"",
        "\"frame\":1",
        "\"submission\":9",
        "\"surface_copy\":false",
        "\"flags\":16",
        "\"image_type\":1",
        "\"format\":37",
        "\"width\":17",
        "\"height\":19",
        "\"depth\":1",
        "\"mip_levels\":4",
        "\"array_layers\":6",
        "\"samples\":1",
        "\"tiling\":0",
        "\"usage\":6",
        "\"sharing_mode\":0",
        "\"s_type\":14",
        "\"initial_layout\":0",
        "\"allocation_flags\":1",
        "\"allocation_usage\":8",
        "\"required_flags\":3",
        "\"preferred_flags\":5",
        "\"memory_type_bits\":7",
        "\"priority_bits\":1056964608",
        "\"min_alignment\":256",
        "\"size_bytes\":123456",
        "\"memory_type\":2",
        "\"image\":\"0000000000001001\"",
        "\"allocation\":\"0000000000002001\"",
        "\"key_supported\":true",
        "\"complete\":true",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(fields); i++) {
        has(output, fields[i]);
    }
    g_free(output);
}

static void test_allocation_failure(void)
{
    reset();
    next_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    char *path = new_path();
    PGRAPHVkTextureAllocationTrace *trace =
        pgraph_vk_texture_allocation_trace_open(path, 10);
    create(trace, true);
    g_assert_cmpuint(creates, ==, 1);
    g_assert_cmpuint(queries, ==, 0);
    g_assert_true(pgraph_vk_texture_allocation_trace_close(trace));
    char *output = read_and_remove(path);
    has(output, "\"result\":-2");
    has(output, "\"successful_creates\":0");
    has(output, "\"surface_copy\":true");
    has(output, "\"size_bytes\":0");
    g_free(output);
}

static void test_unsupported_key(void)
{
    for (unsigned i = 0; i < 5; i++) {
        reset();
        char *path = new_path();
        PGRAPHVkTextureAllocationTrace *trace =
            pgraph_vk_texture_allocation_trace_open(path, 10);
        VkImageCreateInfo info = image_info;
        VmaAllocationCreateInfo policy = alloc_info;
        switch (i) {
        case 0:
            info.pNext = &image_info;
            break;
        case 1:
            info.queueFamilyIndexCount = 1;
            break;
        case 2:
            policy.pool = (VmaPool)(uintptr_t)0x1;
            break;
        case 3:
            policy.pUserData = &policy;
            break;
        case 4:
            info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            break;
        }
        expected_image_info = &info;
        expected_alloc_info = &policy;
        create(trace, false);
        g_assert_true(pgraph_vk_texture_allocation_trace_close(trace));
        char *output = read_and_remove(path);
        has(output, "\"key_supported\":false");
        g_free(output);
    }
}

static void test_teardown_and_counts(void)
{
    reset();
    char *path = new_path();
    PGRAPHVkTextureAllocationTrace *trace =
        pgraph_vk_texture_allocation_trace_open(path, 10);
    create(trace, false);
    pgraph_vk_texture_image_destroy(trace, allocator, created_image,
                                    created_allocation, 10);
    create(trace, true);
    pgraph_vk_texture_allocation_trace_teardown(trace);
    pgraph_vk_texture_image_destroy(trace, allocator, created_image,
                                    created_allocation, 11);
    g_assert_cmpuint(creates, ==, 2);
    g_assert_cmpuint(destroys, ==, 2);
    g_assert_true(pgraph_vk_texture_allocation_trace_close(trace));
    char *output = read_and_remove(path);
    has(output, "\"seq\":4");
    has(output, "\"teardown\":false");
    has(output, "\"teardown\":true");
    has(output, "\"create_calls\":2");
    has(output, "\"successful_creates\":2");
    has(output, "\"destroy_calls\":2");
    has(output, "\"records\":4");
    const char *fixture = g_getenv("XEMU_TEST_ALLOCATION_TRACE_PATH");
    if (fixture) {
        g_assert_true(g_file_set_contents(fixture, output, -1, NULL));
    }
    g_free(output);
}

static void test_null_destruction(void)
{
    reset();
    char *path = new_path();
    PGRAPHVkTextureAllocationTrace *trace =
        pgraph_vk_texture_allocation_trace_open(path, 10);
    pgraph_vk_texture_image_destroy(trace, allocator, VK_NULL_HANDLE, NULL, 0);
    g_assert_cmpuint(destroys, ==, 1);
    g_assert_true(pgraph_vk_texture_allocation_trace_close(trace));
    char *output = read_and_remove(path);
    has(output, "\"image\":\"0000000000000000\"");
    has(output, "\"destroy_calls\":1");
    g_free(output);
}

static void test_shutdown_checkpoint(void)
{
    reset();
    char *path = new_path();
    PGRAPHVkTextureAllocationTrace *trace =
        pgraph_vk_texture_allocation_trace_open(path, 10);
    create(trace, false);
    g_assert_true(
        pgraph_vk_texture_allocation_trace_shutdown_checkpoint(trace));
    g_assert_cmpuint(creates, ==, 1);
    g_assert_cmpuint(destroys, ==, 0);
    char *output = read_and_remove(path);
    has(output, "\"end_reason\":\"shutdown_checkpoint\"");
    has(output, "\"create_calls\":1");
    has(output, "\"destroy_calls\":0");
    has(output, "\"complete\":true");
    g_assert_null(strstr(output, "\"type\":\"destroy\""));
    const char *fixture = g_getenv("XEMU_TEST_ALLOCATION_CHECKPOINT_PATH");
    if (fixture) {
        g_assert_true(g_file_set_contents(fixture, output, -1, NULL));
    }
    g_free(output);
    g_assert_true(pgraph_vk_texture_allocation_trace_shutdown_checkpoint(NULL));
}

static void test_record_cap(void)
{
    reset();
    char *path = new_path();
    PGRAPHVkTextureAllocationTrace *trace =
        pgraph_vk_texture_allocation_trace_open(path, 1);
    create(trace, false);
    create(trace, false);
    g_assert_false(pgraph_vk_texture_allocation_trace_close(trace));
    char *output = read_and_remove(path);
    has(output, "\"records\":1");
    has(output, "\"dropped\":1");
    has(output, "\"complete\":false");
    has(output, "\"create_calls\":2");
    g_assert_null(strstr(output, "\"seq\":2"));
    g_free(output);
}

static void test_existing_output(void)
{
    char *path = new_path();
    g_assert_true(g_file_set_contents(path, "keep", -1, NULL));
    g_assert_null(pgraph_vk_texture_allocation_trace_open(path, 10));
    char *output = read_and_remove(path);
    g_assert_cmpstr(output, ==, "keep");
    g_free(output);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/texture-allocation/disabled", test_disabled);
    g_test_add_func("/texture-allocation/actual-configuration",
                    test_actual_configuration);
    g_test_add_func("/texture-allocation/allocation-failure",
                    test_allocation_failure);
    g_test_add_func("/texture-allocation/unsupported-key",
                    test_unsupported_key);
    g_test_add_func("/texture-allocation/teardown-and-counts",
                    test_teardown_and_counts);
    g_test_add_func("/texture-allocation/record-cap", test_record_cap);
    g_test_add_func("/texture-allocation/null-destruction",
                    test_null_destruction);
    g_test_add_func("/texture-allocation/shutdown-checkpoint",
                    test_shutdown_checkpoint);
    g_test_add_func("/texture-allocation/existing-output",
                    test_existing_output);
    return g_test_run();
}
