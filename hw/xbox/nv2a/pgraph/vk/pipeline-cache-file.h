/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_PIPELINE_CACHE_FILE_H
#define HW_XBOX_NV2A_PGRAPH_VK_PIPELINE_CACHE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define PGRAPH_VK_PIPELINE_CACHE_FILE_MAGIC UINT64_C(0x58454d55564b5043)
#define PGRAPH_VK_PIPELINE_CACHE_FILE_SCHEMA 1
#define PGRAPH_VK_PIPELINE_CACHE_MAX_DATA_SIZE (64U * 1024U * 1024U)
#define PGRAPH_VK_PIPELINE_CACHE_HASH_OFFSET UINT64_C(14695981039346656037)

typedef struct PGRAPHVkPipelineCacheIdentity {
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t driver_version;
    uint32_t api_version;
    uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
    uint64_t build_hash;
} PGRAPHVkPipelineCacheIdentity;

typedef struct PGRAPHVkPipelineCacheFileHeader {
    uint64_t magic;
    uint32_t schema;
    uint32_t header_size;
    PGRAPHVkPipelineCacheIdentity identity;
    uint64_t data_size;
    uint64_t data_hash;
} PGRAPHVkPipelineCacheFileHeader;

typedef struct PGRAPHVkPipelineCacheDriverHeader {
    uint32_t header_size;
    uint32_t header_version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
} PGRAPHVkPipelineCacheDriverHeader;

static inline uint64_t pgraph_vk_pipeline_cache_hash_update(
    uint64_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = data;

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static inline uint64_t pgraph_vk_pipeline_cache_hash(const void *data,
                                                     size_t size)
{
    return pgraph_vk_pipeline_cache_hash_update(
        PGRAPH_VK_PIPELINE_CACHE_HASH_OFFSET, data, size);
}

static inline bool pgraph_vk_pipeline_cache_identity_equal(
    const PGRAPHVkPipelineCacheIdentity *a,
    const PGRAPHVkPipelineCacheIdentity *b)
{
    return a->vendor_id == b->vendor_id && a->device_id == b->device_id &&
           a->driver_version == b->driver_version &&
           a->api_version == b->api_version &&
           !memcmp(a->pipeline_cache_uuid, b->pipeline_cache_uuid,
                   VK_UUID_SIZE) &&
           a->build_hash == b->build_hash;
}

static inline void pgraph_vk_pipeline_cache_header_init(
    PGRAPHVkPipelineCacheFileHeader *header,
    const PGRAPHVkPipelineCacheIdentity *identity, const void *data,
    size_t data_size)
{
    *header = (PGRAPHVkPipelineCacheFileHeader) {
        .magic = PGRAPH_VK_PIPELINE_CACHE_FILE_MAGIC,
        .schema = PGRAPH_VK_PIPELINE_CACHE_FILE_SCHEMA,
        .header_size = sizeof(*header),
        .identity = *identity,
        .data_size = data_size,
        .data_hash = pgraph_vk_pipeline_cache_hash(data, data_size),
    };
}

static inline bool pgraph_vk_pipeline_cache_file_validate(
    const void *file_data, size_t file_size,
    const PGRAPHVkPipelineCacheIdentity *identity, const uint8_t **cache_data,
    size_t *cache_data_size)
{
    PGRAPHVkPipelineCacheFileHeader header;

    if (file_size < sizeof(header)) {
        return false;
    }
    memcpy(&header, file_data, sizeof(header));

    if (header.magic != PGRAPH_VK_PIPELINE_CACHE_FILE_MAGIC ||
        header.schema != PGRAPH_VK_PIPELINE_CACHE_FILE_SCHEMA ||
        header.header_size != sizeof(header) ||
        !pgraph_vk_pipeline_cache_identity_equal(&header.identity, identity) ||
        header.data_size > PGRAPH_VK_PIPELINE_CACHE_MAX_DATA_SIZE ||
        header.data_size != file_size - sizeof(header)) {
        return false;
    }

    const uint8_t *payload = (const uint8_t *)file_data + sizeof(header);
    if (header.data_hash !=
        pgraph_vk_pipeline_cache_hash(payload, header.data_size)) {
        return false;
    }

    PGRAPHVkPipelineCacheDriverHeader driver_header;
    if (header.data_size < sizeof(driver_header)) {
        return false;
    }
    memcpy(&driver_header, payload, sizeof(driver_header));
    if (driver_header.header_size < sizeof(driver_header) ||
        driver_header.header_size > header.data_size ||
        driver_header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
        driver_header.vendor_id != identity->vendor_id ||
        driver_header.device_id != identity->device_id ||
        memcmp(driver_header.pipeline_cache_uuid,
               identity->pipeline_cache_uuid, VK_UUID_SIZE)) {
        return false;
    }

    if (cache_data) {
        *cache_data = payload;
    }
    if (cache_data_size) {
        *cache_data_size = header.data_size;
    }
    return true;
}

#endif
