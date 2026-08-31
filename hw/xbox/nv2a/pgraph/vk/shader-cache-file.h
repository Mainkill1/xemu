/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_SHADER_CACHE_FILE_H
#define HW_XBOX_NV2A_PGRAPH_VK_SHADER_CACHE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PGRAPH_VK_SHADER_CACHE_FILE_MAGIC UINT64_C(0x58454d5556535056)
#define PGRAPH_VK_SHADER_CACHE_FILE_SCHEMA 1
#define PGRAPH_VK_SHADER_CACHE_HASH_OFFSET UINT64_C(14695981039346656037)
#define PGRAPH_VK_SHADER_CACHE_MAX_SOURCE_SIZE (2U * 1024U * 1024U)
#define PGRAPH_VK_SHADER_CACHE_MAX_SPIRV_SIZE (4U * 1024U * 1024U)
#define PGRAPH_VK_SHADER_CACHE_MAX_FILES 8192U
#define PGRAPH_VK_SHADER_CACHE_MAX_TOTAL_SIZE (256U * 1024U * 1024U)

enum {
    PGRAPH_VK_SHADER_CACHE_FLAG_DEBUG = 1U << 0,
    PGRAPH_VK_SHADER_CACHE_FLAG_VALIDATE = 1U << 1,
};

typedef struct PGRAPHVkShaderCacheIdentity {
    uint32_t stage;
    uint32_t flags;
    uint32_t client;
    uint32_t client_version;
    uint32_t target_language;
    uint32_t target_language_version;
    uint32_t default_version;
    uint32_t default_profile;
    uint32_t messages;
    uint32_t glslang_major;
    uint32_t glslang_minor;
    uint32_t glslang_patch;
    uint64_t glslang_flavor_hash;
    uint64_t resource_hash;
    uint64_t build_hash;
} PGRAPHVkShaderCacheIdentity;

typedef struct PGRAPHVkShaderCacheFileHeader {
    uint64_t magic;
    uint32_t schema;
    uint32_t header_size;
    PGRAPHVkShaderCacheIdentity identity;
    uint64_t source_size;
    uint64_t source_hash;
    uint64_t spirv_size;
    uint64_t spirv_hash;
} PGRAPHVkShaderCacheFileHeader;

static inline uint64_t pgraph_vk_shader_cache_hash_update(
    uint64_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = data;

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static inline uint64_t pgraph_vk_shader_cache_hash(const void *data,
                                                   size_t size)
{
    return pgraph_vk_shader_cache_hash_update(
        PGRAPH_VK_SHADER_CACHE_HASH_OFFSET, data, size);
}

static inline bool pgraph_vk_shader_cache_identity_equal(
    const PGRAPHVkShaderCacheIdentity *a,
    const PGRAPHVkShaderCacheIdentity *b)
{
    return a->stage == b->stage && a->flags == b->flags &&
           a->client == b->client &&
           a->client_version == b->client_version &&
           a->target_language == b->target_language &&
           a->target_language_version == b->target_language_version &&
           a->default_version == b->default_version &&
           a->default_profile == b->default_profile &&
           a->messages == b->messages &&
           a->glslang_major == b->glslang_major &&
           a->glslang_minor == b->glslang_minor &&
           a->glslang_patch == b->glslang_patch &&
           a->glslang_flavor_hash == b->glslang_flavor_hash &&
           a->resource_hash == b->resource_hash &&
           a->build_hash == b->build_hash;
}

static inline uint64_t pgraph_vk_shader_cache_key_hash(
    const PGRAPHVkShaderCacheIdentity *identity, const void *source,
    size_t source_size)
{
    uint64_t hash = PGRAPH_VK_SHADER_CACHE_HASH_OFFSET;
#define HASH_IDENTITY_FIELD(field)                                      \
    hash = pgraph_vk_shader_cache_hash_update(                          \
        hash, &identity->field, sizeof(identity->field))
    HASH_IDENTITY_FIELD(stage);
    HASH_IDENTITY_FIELD(flags);
    HASH_IDENTITY_FIELD(client);
    HASH_IDENTITY_FIELD(client_version);
    HASH_IDENTITY_FIELD(target_language);
    HASH_IDENTITY_FIELD(target_language_version);
    HASH_IDENTITY_FIELD(default_version);
    HASH_IDENTITY_FIELD(default_profile);
    HASH_IDENTITY_FIELD(messages);
    HASH_IDENTITY_FIELD(glslang_major);
    HASH_IDENTITY_FIELD(glslang_minor);
    HASH_IDENTITY_FIELD(glslang_patch);
    HASH_IDENTITY_FIELD(glslang_flavor_hash);
    HASH_IDENTITY_FIELD(resource_hash);
    HASH_IDENTITY_FIELD(build_hash);
#undef HASH_IDENTITY_FIELD
    return pgraph_vk_shader_cache_hash_update(hash, source, source_size);
}

static inline bool pgraph_vk_shader_cache_spirv_validate(
    const void *spirv, size_t spirv_size)
{
    uint32_t header[5];

    if (spirv_size < sizeof(header) || spirv_size % sizeof(uint32_t) ||
        spirv_size > PGRAPH_VK_SHADER_CACHE_MAX_SPIRV_SIZE) {
        return false;
    }
    memcpy(header, spirv, sizeof(header));
    return header[0] == UINT32_C(0x07230203) &&
           header[1] >= UINT32_C(0x00010000) &&
           header[1] <= UINT32_C(0x00010600) && header[3] != 0 &&
           header[4] == 0;
}

static inline bool pgraph_vk_shader_cache_header_init(
    PGRAPHVkShaderCacheFileHeader *header,
    const PGRAPHVkShaderCacheIdentity *identity, const void *source,
    size_t source_size, const void *spirv, size_t spirv_size)
{
    if (!source_size || source_size > PGRAPH_VK_SHADER_CACHE_MAX_SOURCE_SIZE ||
        !pgraph_vk_shader_cache_spirv_validate(spirv, spirv_size)) {
        return false;
    }

    *header = (PGRAPHVkShaderCacheFileHeader) {
        .magic = PGRAPH_VK_SHADER_CACHE_FILE_MAGIC,
        .schema = PGRAPH_VK_SHADER_CACHE_FILE_SCHEMA,
        .header_size = sizeof(*header),
        .identity = *identity,
        .source_size = source_size,
        .source_hash = pgraph_vk_shader_cache_hash(source, source_size),
        .spirv_size = spirv_size,
        .spirv_hash = pgraph_vk_shader_cache_hash(spirv, spirv_size),
    };
    return true;
}

static inline bool pgraph_vk_shader_cache_file_validate(
    const void *file_data, size_t file_size,
    const PGRAPHVkShaderCacheIdentity *identity, const void *source,
    size_t source_size, const uint8_t **spirv, size_t *spirv_size)
{
    PGRAPHVkShaderCacheFileHeader header;

    if (file_size < sizeof(header) || !source_size ||
        source_size > PGRAPH_VK_SHADER_CACHE_MAX_SOURCE_SIZE) {
        return false;
    }
    memcpy(&header, file_data, sizeof(header));

    if (header.magic != PGRAPH_VK_SHADER_CACHE_FILE_MAGIC ||
        header.schema != PGRAPH_VK_SHADER_CACHE_FILE_SCHEMA ||
        header.header_size != sizeof(header) ||
        !pgraph_vk_shader_cache_identity_equal(&header.identity, identity) ||
        header.source_size != source_size ||
        header.source_hash != pgraph_vk_shader_cache_hash(source, source_size) ||
        header.spirv_size > PGRAPH_VK_SHADER_CACHE_MAX_SPIRV_SIZE ||
        header.source_size > SIZE_MAX - sizeof(header) ||
        header.spirv_size >
            SIZE_MAX - sizeof(header) - (size_t)header.source_size) {
        return false;
    }

    size_t payload_offset = sizeof(header) + (size_t)header.source_size;
    if (payload_offset > file_size ||
        (size_t)header.spirv_size != file_size - payload_offset ||
        memcmp((const uint8_t *)file_data + sizeof(header), source,
               source_size)) {
        return false;
    }

    const uint8_t *payload = (const uint8_t *)file_data + payload_offset;
    if (!pgraph_vk_shader_cache_spirv_validate(payload, header.spirv_size) ||
        header.spirv_hash !=
            pgraph_vk_shader_cache_hash(payload, header.spirv_size)) {
        return false;
    }

    if (spirv) {
        *spirv = payload;
    }
    if (spirv_size) {
        *spirv_size = header.spirv_size;
    }
    return true;
}

static inline bool pgraph_vk_shader_cache_budget_allows(
    size_t file_count, uint64_t total_size, bool replacing,
    uint64_t replaced_size, uint64_t new_size)
{
    if (new_size > PGRAPH_VK_SHADER_CACHE_MAX_TOTAL_SIZE ||
        (replacing && (!file_count || replaced_size > total_size))) {
        return false;
    }
    if (replacing) {
        total_size -= replaced_size;
    } else {
        if (file_count >= PGRAPH_VK_SHADER_CACHE_MAX_FILES) {
            return false;
        }
        file_count++;
    }
    return total_size <= PGRAPH_VK_SHADER_CACHE_MAX_TOTAL_SIZE - new_size &&
           file_count <= PGRAPH_VK_SHADER_CACHE_MAX_FILES;
}

#endif
