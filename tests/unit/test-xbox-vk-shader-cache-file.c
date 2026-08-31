/*
 * Vulkan NV2A shader artifact cache file tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/shader-cache-file.h"

static const char test_source[] = "#version 460\nvoid main() {}\n";

static PGRAPHVkShaderCacheIdentity test_identity(void)
{
    return (PGRAPHVkShaderCacheIdentity) {
        .stage = 4,
        .flags = PGRAPH_VK_SHADER_CACHE_FLAG_VALIDATE,
        .client = 1,
        .client_version = 0x00403000,
        .target_language = 1,
        .target_language_version = 0x00010600,
        .default_version = 460,
        .default_profile = 0,
        .messages = 1,
        .glslang_major = 16,
        .glslang_minor = 5,
        .glslang_patch = 0,
        .glslang_flavor_hash = UINT64_C(0x123456789abcdef0),
        .resource_hash = UINT64_C(0x23456789abcdef01),
        .build_hash = UINT64_C(0x3456789abcdef012),
    };
}

static GByteArray *test_spirv(void)
{
    const uint32_t words[] = {
        UINT32_C(0x07230203), UINT32_C(0x00010600), 0, 1, 0,
    };
    return g_byte_array_new_take(g_memdup2(words, sizeof(words)),
                                 sizeof(words));
}

static GByteArray *test_file(const PGRAPHVkShaderCacheIdentity *identity)
{
    g_autoptr(GByteArray) spirv = test_spirv();
    PGRAPHVkShaderCacheFileHeader header;
    g_assert_true(pgraph_vk_shader_cache_header_init(
        &header, identity, test_source, strlen(test_source), spirv->data,
        spirv->len));

    GByteArray *file = g_byte_array_sized_new(
        sizeof(header) + strlen(test_source) + spirv->len);
    g_byte_array_append(file, (const uint8_t *)&header, sizeof(header));
    g_byte_array_append(file, (const uint8_t *)test_source,
                        strlen(test_source));
    g_byte_array_append(file, spirv->data, spirv->len);
    return file;
}

static void test_valid_file(void)
{
    PGRAPHVkShaderCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);
    const uint8_t *spirv = NULL;
    size_t spirv_size = 0;

    g_assert_true(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        &spirv, &spirv_size));
    g_assert_cmpuint(spirv_size, ==, 5 * sizeof(uint32_t));
    uint32_t magic;
    memcpy(&magic, spirv, sizeof(magic));
    g_assert_cmphex(magic, ==, UINT32_C(0x07230203));
}

static void test_identity_and_key(void)
{
    PGRAPHVkShaderCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);

#define CHECK_FIELD(field)                                                \
    do {                                                                  \
        PGRAPHVkShaderCacheIdentity other = identity;                     \
        other.field++;                                                    \
        g_assert_false(pgraph_vk_shader_cache_file_validate(              \
            file->data, file->len, &other, test_source,                   \
            strlen(test_source), NULL, NULL));                            \
    } while (0)
    CHECK_FIELD(stage);
    CHECK_FIELD(flags);
    CHECK_FIELD(client);
    CHECK_FIELD(client_version);
    CHECK_FIELD(target_language);
    CHECK_FIELD(target_language_version);
    CHECK_FIELD(default_version);
    CHECK_FIELD(default_profile);
    CHECK_FIELD(messages);
    CHECK_FIELD(glslang_major);
    CHECK_FIELD(glslang_minor);
    CHECK_FIELD(glslang_patch);
    CHECK_FIELD(glslang_flavor_hash);
    CHECK_FIELD(resource_hash);
    CHECK_FIELD(build_hash);
#undef CHECK_FIELD

    PGRAPHVkShaderCacheIdentity padded;
    memset(&padded, 0xa5, sizeof(padded));
#define COPY_FIELD(field) padded.field = identity.field
    COPY_FIELD(stage);
    COPY_FIELD(flags);
    COPY_FIELD(client);
    COPY_FIELD(client_version);
    COPY_FIELD(target_language);
    COPY_FIELD(target_language_version);
    COPY_FIELD(default_version);
    COPY_FIELD(default_profile);
    COPY_FIELD(messages);
    COPY_FIELD(glslang_major);
    COPY_FIELD(glslang_minor);
    COPY_FIELD(glslang_patch);
    COPY_FIELD(glslang_flavor_hash);
    COPY_FIELD(resource_hash);
    COPY_FIELD(build_hash);
#undef COPY_FIELD
    g_assert_cmphex(pgraph_vk_shader_cache_key_hash(
                        &padded, test_source, strlen(test_source)),
                    ==,
                    pgraph_vk_shader_cache_key_hash(
                        &identity, test_source, strlen(test_source)));
}

static void test_wrong_or_corrupt_source(void)
{
    PGRAPHVkShaderCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);
    g_autofree char *other_source = g_strdup(test_source);
    other_source[strlen(other_source) - 2] ^= 1;

    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, other_source, strlen(other_source),
        NULL, NULL));

    PGRAPHVkShaderCacheFileHeader *header = (void *)file->data;
    uint8_t *stored_source = file->data + sizeof(*header);
    stored_source[0] ^= 1;
    header->source_hash = pgraph_vk_shader_cache_hash(
        stored_source, header->source_size);
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        NULL, NULL));
}

static void test_corrupt_spirv(void)
{
    PGRAPHVkShaderCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);
    PGRAPHVkShaderCacheFileHeader *header = (void *)file->data;

    header->magic++;
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        NULL, NULL));
    header->magic--;
    header->schema++;
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        NULL, NULL));
    header->schema--;
    header->header_size++;
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        NULL, NULL));
    header->header_size--;
    uint8_t *spirv = file->data + sizeof(*header) + header->source_size;

    spirv[0] ^= 1;
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        NULL, NULL));
    spirv[0] ^= 1;

    const size_t indices[] = { 0, 1, 3, 4 };
    const uint32_t invalid[] = { 0, UINT32_C(0x00010700), 0, 1 };
    for (size_t i = 0; i < G_N_ELEMENTS(indices); i++) {
        uint32_t saved;
        memcpy(&saved, spirv + indices[i] * sizeof(uint32_t), sizeof(saved));
        memcpy(spirv + indices[i] * sizeof(uint32_t), &invalid[i],
               sizeof(invalid[i]));
        header->spirv_hash = pgraph_vk_shader_cache_hash(
            spirv, header->spirv_size);
        g_assert_false(pgraph_vk_shader_cache_file_validate(
            file->data, file->len, &identity, test_source,
            strlen(test_source), NULL, NULL));
        memcpy(spirv + indices[i] * sizeof(uint32_t), &saved, sizeof(saved));
    }
}

static void test_truncation_extension_and_overflow(void)
{
    PGRAPHVkShaderCacheIdentity identity = test_identity();
    g_autoptr(GByteArray) file = test_file(&identity);
    PGRAPHVkShaderCacheFileHeader *header = (void *)file->data;

    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, sizeof(*header) - 1, &identity, test_source,
        strlen(test_source), NULL, NULL));
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len - 1, &identity, test_source,
        strlen(test_source), NULL, NULL));
    g_byte_array_append(file, (const uint8_t *)"x", 1);
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        NULL, NULL));
    g_byte_array_set_size(file, file->len - 1);

    header->spirv_size = UINT64_MAX;
    g_assert_false(pgraph_vk_shader_cache_file_validate(
        file->data, file->len, &identity, test_source, strlen(test_source),
        NULL, NULL));

    g_autoptr(GByteArray) spirv = test_spirv();
    g_assert_false(pgraph_vk_shader_cache_spirv_validate(
        spirv->data, spirv->len - 1));
    g_assert_false(pgraph_vk_shader_cache_header_init(
        header, &identity, test_source, 0, spirv->data, spirv->len));
}

static void test_budget(void)
{
    g_assert_true(pgraph_vk_shader_cache_budget_allows(0, 0, false, 0, 1));
    g_assert_false(pgraph_vk_shader_cache_budget_allows(
        PGRAPH_VK_SHADER_CACHE_MAX_FILES, 0, false, 0, 1));
    g_assert_false(pgraph_vk_shader_cache_budget_allows(
        1, PGRAPH_VK_SHADER_CACHE_MAX_TOTAL_SIZE, false, 0, 1));
    g_assert_true(pgraph_vk_shader_cache_budget_allows(
        PGRAPH_VK_SHADER_CACHE_MAX_FILES,
        PGRAPH_VK_SHADER_CACHE_MAX_TOTAL_SIZE, true, 16, 16));
    g_assert_false(pgraph_vk_shader_cache_budget_allows(
        0, 0, true, 0, 1));
    g_assert_false(pgraph_vk_shader_cache_budget_allows(
        1, 8, true, 9, 1));
    g_assert_false(pgraph_vk_shader_cache_budget_allows(
        1, UINT64_MAX, false, 0, 1));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-shader-cache-file/valid", test_valid_file);
    g_test_add_func("/xbox/vk-shader-cache-file/identity-key",
                    test_identity_and_key);
    g_test_add_func("/xbox/vk-shader-cache-file/source",
                    test_wrong_or_corrupt_source);
    g_test_add_func("/xbox/vk-shader-cache-file/spirv", test_corrupt_spirv);
    g_test_add_func("/xbox/vk-shader-cache-file/size-overflow",
                    test_truncation_extension_and_overflow);
    g_test_add_func("/xbox/vk-shader-cache-file/budget", test_budget);
    return g_test_run();
}
