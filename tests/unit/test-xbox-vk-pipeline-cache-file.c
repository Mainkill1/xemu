/*
 * Vulkan NV2A pipeline cache file tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/pipeline-cache-file.h"

static PGRAPHVkPipelineCacheIdentity test_identity(void)
{
    PGRAPHVkPipelineCacheIdentity identity = {
        .vendor_id = 0x1234,
        .device_id = 0x5678,
        .driver_version = 0x01020304,
        .api_version = VK_API_VERSION_1_3,
        .build_hash = UINT64_C(0x1020304050607080),
    };
    for (size_t i = 0; i < VK_UUID_SIZE; i++) {
        identity.pipeline_cache_uuid[i] = i;
    }
    return identity;
}

static GByteArray *test_file(const PGRAPHVkPipelineCacheIdentity *identity)
{
    PGRAPHVkPipelineCacheDriverHeader driver_header = {
        .header_size = sizeof(driver_header),
        .header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE,
        .vendor_id = identity->vendor_id,
        .device_id = identity->device_id,
    };
    memcpy(driver_header.pipeline_cache_uuid, identity->pipeline_cache_uuid,
           VK_UUID_SIZE);
    static const uint8_t driver_data[] = { 1, 3, 3, 7, 9 };

    g_autoptr(GByteArray) payload =
        g_byte_array_sized_new(sizeof(driver_header) + sizeof(driver_data));
    g_byte_array_append(payload, (const uint8_t *)&driver_header,
                        sizeof(driver_header));
    g_byte_array_append(payload, driver_data, sizeof(driver_data));

    PGRAPHVkPipelineCacheFileHeader header;
    pgraph_vk_pipeline_cache_header_init(&header, identity, payload->data,
                                         payload->len);

    GByteArray *file = g_byte_array_sized_new(sizeof(header) + payload->len);
    g_byte_array_append(file, (const uint8_t *)&header, sizeof(header));
    g_byte_array_append(file, payload->data, payload->len);
    return file;
}

static void test_valid_file(void)
{
    PGRAPHVkPipelineCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);
    const uint8_t *payload = NULL;
    size_t payload_size = 0;

    g_assert_true(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, &payload, &payload_size));
    g_assert_cmpuint(payload_size, ==,
                     sizeof(PGRAPHVkPipelineCacheDriverHeader) + 5);
    g_assert_cmpuint(
        payload[sizeof(PGRAPHVkPipelineCacheDriverHeader) + 3], ==, 7);
}

static void test_identity_mismatch(void)
{
    PGRAPHVkPipelineCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);

#define CHECK_FIELD(field) \
    do {                   \
        PGRAPHVkPipelineCacheIdentity other = identity; \
        other.field++;                                      \
        g_assert_false(pgraph_vk_pipeline_cache_file_validate( \
            file->data, file->len, &other, NULL, NULL));       \
    } while (0)

    CHECK_FIELD(vendor_id);
    CHECK_FIELD(device_id);
    CHECK_FIELD(driver_version);
    CHECK_FIELD(api_version);
    CHECK_FIELD(build_hash);

#undef CHECK_FIELD

    PGRAPHVkPipelineCacheIdentity other = identity;
    other.pipeline_cache_uuid[VK_UUID_SIZE - 1]++;
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &other, NULL, NULL));
}

static void test_corrupt_file(void)
{
    PGRAPHVkPipelineCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);
    PGRAPHVkPipelineCacheFileHeader *header = (void *)file->data;

    file->data[file->len - 1] ^= 1;
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, NULL, NULL));
    file->data[file->len - 1] ^= 1;

    header->magic++;
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, NULL, NULL));
    header->magic--;

    header->schema++;
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, NULL, NULL));
    header->schema--;

    header->header_size++;
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, NULL, NULL));
    header->header_size--;

    PGRAPHVkPipelineCacheDriverHeader *driver_header =
        (void *)(file->data + sizeof(*header));
    driver_header->vendor_id++;
    header->data_hash = pgraph_vk_pipeline_cache_hash(
        driver_header, header->data_size);
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, NULL, NULL));
    driver_header->vendor_id--;

    header->data_size = PGRAPH_VK_PIPELINE_CACHE_MAX_DATA_SIZE + 1ULL;
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, NULL, NULL));
}

static void test_truncated_or_extended_file(void)
{
    PGRAPHVkPipelineCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);

    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, sizeof(PGRAPHVkPipelineCacheFileHeader) - 1, &identity,
        NULL, NULL));
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len - 1, &identity, NULL, NULL));

    g_byte_array_append(file, (const uint8_t *)"x", 1);
    g_assert_false(pgraph_vk_pipeline_cache_file_validate(
        file->data, file->len, &identity, NULL, NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-pipeline-cache-file/valid", test_valid_file);
    g_test_add_func("/xbox/vk-pipeline-cache-file/identity-mismatch",
                    test_identity_mismatch);
    g_test_add_func("/xbox/vk-pipeline-cache-file/corrupt",
                    test_corrupt_file);
    g_test_add_func("/xbox/vk-pipeline-cache-file/size",
                    test_truncated_or_extended_file);
    return g_test_run();
}
