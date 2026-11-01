/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "qemu/osdep.h"

#include "texture-allocation-trace.h"

struct PGRAPHVkTextureAllocationTrace {
    FILE *file;
    uint64_t max_records;
    uint64_t records;
    uint64_t dropped;
    uint64_t frame;
    uint64_t create_calls;
    uint64_t successful_creates;
    uint64_t destroy_calls;
    uint64_t create_elapsed_us;
    uint64_t destroy_elapsed_us;
    uint64_t create_max_us;
    uint64_t destroy_max_us;
    uint64_t clock_errors;
    bool teardown;
    bool shutdown_checkpoint;
    bool io_failed;
};

static const char *json_bool(bool value)
{
    return value ? "true" : "false";
}

static uint64_t image_id(VkImage image)
{
#if VK_USE_64_BIT_PTR_DEFINES
    return (uint64_t)(uintptr_t)image;
#else
    return image;
#endif
}

PGRAPHVkTextureAllocationTrace *
pgraph_vk_texture_allocation_trace_open(const char *path, uint64_t max_records)
{
    if (!path || !path[0] || !max_records) {
        return NULL;
    }
    FILE *file = qemu_fopen(path, "wx");
    if (!file) {
        return NULL;
    }
    if (fprintf(file,
                "{\"type\":\"schema\",\"schema_version\":2,"
                "\"duration_unit\":\"host_elapsed_us\","
                "\"frame_meaning\":\"completed_renderer_flip_stalls\","
                "\"scope\":\"ordinary_texture_cache_including_surface_copy_"
                "excluding_dummy\","
                "\"max_records\":%" PRIu64 "}\n",
                max_records) < 0 ||
        fflush(file) != 0) {
        fclose(file);
        return NULL;
    }
    PGRAPHVkTextureAllocationTrace *trace =
        g_new0(PGRAPHVkTextureAllocationTrace, 1);
    trace->file = file;
    trace->max_records = max_records;
    return trace;
}

bool pgraph_vk_texture_allocation_trace_close(
    PGRAPHVkTextureAllocationTrace *trace)
{
    if (!trace) {
        return true;
    }
    /* Resolve buffered event writes before declaring ledger completeness. */
    trace->io_failed |= fflush(trace->file) != 0;
    bool complete =
        !trace->dropped && !trace->io_failed && !trace->clock_errors;
    int result = fprintf(
        trace->file,
        "{\"type\":\"summary\",\"records\":%" PRIu64 ",\"dropped\":%" PRIu64
        ",\"complete\":%s"
        ",\"end_reason\":\"%s\""
        ",\"clock_errors\":%" PRIu64 ",\"create_calls\":%" PRIu64
        ",\"successful_creates\":%" PRIu64 ",\"destroy_calls\":%" PRIu64
        ",\"create_elapsed_total_us\":%" PRIu64
        ",\"destroy_elapsed_total_us\":%" PRIu64
        ",\"create_elapsed_max_us\":%" PRIu64
        ",\"destroy_elapsed_max_us\":%" PRIu64 "}\n",
        trace->records, trace->dropped, json_bool(complete),
        trace->shutdown_checkpoint ? "shutdown_checkpoint" :
                                     "renderer_teardown",
        trace->clock_errors, trace->create_calls, trace->successful_creates,
        trace->destroy_calls, trace->create_elapsed_us,
        trace->destroy_elapsed_us, trace->create_max_us, trace->destroy_max_us);
    complete &= result >= 0;
    complete &= fflush(trace->file) == 0;
    complete &= fclose(trace->file) == 0;
    g_free(trace);
    return complete;
}

bool pgraph_vk_texture_allocation_trace_shutdown_checkpoint(
    PGRAPHVkTextureAllocationTrace *trace)
{
    if (trace) {
        trace->shutdown_checkpoint = true;
    }
    return pgraph_vk_texture_allocation_trace_close(trace);
}

void pgraph_vk_texture_allocation_trace_frame(
    PGRAPHVkTextureAllocationTrace *trace)
{
    if (trace) {
        trace->frame++;
    }
}

void pgraph_vk_texture_allocation_trace_teardown(
    PGRAPHVkTextureAllocationTrace *trace)
{
    if (trace) {
        trace->teardown = true;
    }
}

static uint64_t elapsed_us(PGRAPHVkTextureAllocationTrace *trace, int64_t start,
                           int64_t end)
{
    if (end < start) {
        trace->clock_errors++;
        return 0;
    }
    return end - start;
}

static bool reserve_record(PGRAPHVkTextureAllocationTrace *trace)
{
    if (trace->records == trace->max_records || trace->io_failed) {
        trace->dropped++;
        return false;
    }
    trace->records++;
    return true;
}

VkResult pgraph_vk_texture_image_create(
    PGRAPHVkTextureAllocationTrace *trace, VmaAllocator allocator,
    const VkImageCreateInfo *image_info,
    const VmaAllocationCreateInfo *allocation_info, VkImage *image,
    VmaAllocation *allocation, bool surface_copy, uint32_t submission)
{
    if (!trace) {
        return vmaCreateImage(allocator, image_info, allocation_info, image,
                              allocation, NULL);
    }
    int64_t start = g_get_monotonic_time();
    VkResult result = vmaCreateImage(allocator, image_info, allocation_info,
                                     image, allocation, NULL);
    int64_t end = g_get_monotonic_time();
    uint64_t duration = elapsed_us(trace, start, end);
    trace->create_calls++;
    trace->successful_creates += result == VK_SUCCESS;
    trace->create_elapsed_us += duration;
    trace->create_max_us = MAX(trace->create_max_us, duration);
    if (!reserve_record(trace)) {
        return result;
    }

    VmaAllocationInfo info = { 0 };
    if (result == VK_SUCCESS) {
        vmaGetAllocationInfo(allocator, *allocation, &info);
    }
    uint32_t priority_bits;
    G_STATIC_ASSERT(sizeof(priority_bits) == sizeof(allocation_info->priority));
    memcpy(&priority_bits, &allocation_info->priority, sizeof(priority_bits));
    bool supported =
        image_info->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO &&
        image_info->pNext == NULL && image_info->queueFamilyIndexCount == 0 &&
        allocation_info->pool == NULL && allocation_info->pUserData == NULL;
    int written = fprintf(
        trace->file,
        "{\"type\":\"create\",\"seq\":%" PRIu64 ",\"frame\":%" PRIu64
        ",\"submission\":%u"
        ",\"start_us\":%" PRId64 ",\"elapsed_us\":%" PRIu64
        ",\"result\":%d,\"surface_copy\":%s"
        ",\"image\":\"%016" PRIx64 "\",\"allocation\":\"%016" PRIxPTR "\""
        ",\"size_bytes\":%" PRIu64 ",\"memory_type\":%u"
        ",\"key_supported\":%s,\"pnext_present\":%s"
        ",\"queue_family_count\":%u,\"custom_pool\":%s,\"user_data\":%s"
        ",\"config\":{\"s_type\":%u,\"flags\":%u,\"image_type\":%u,\"format\":%"
        "u"
        ",\"width\":%u,\"height\":%u,\"depth\":%u"
        ",\"mip_levels\":%u,\"array_layers\":%u,\"samples\":%u"
        ",\"tiling\":%u,\"usage\":%u,\"sharing_mode\":%u"
        ",\"initial_layout\":%u,\"allocation_flags\":%u"
        ",\"allocation_usage\":%u,\"required_flags\":%u"
        ",\"preferred_flags\":%u,\"memory_type_bits\":%u"
        ",\"priority_bits\":%u,\"min_alignment\":%" PRIu64 "}}\n",
        trace->records, trace->frame, submission, start, duration, result,
        json_bool(surface_copy), result == VK_SUCCESS ? image_id(*image) : 0,
        result == VK_SUCCESS ? (uintptr_t)*allocation : (uintptr_t)0,
        (uint64_t)info.size, info.memoryType, json_bool(supported),
        json_bool(image_info->pNext != NULL), image_info->queueFamilyIndexCount,
        json_bool(allocation_info->pool != NULL),
        json_bool(allocation_info->pUserData != NULL), image_info->sType,
        image_info->flags, image_info->imageType, image_info->format,
        image_info->extent.width, image_info->extent.height,
        image_info->extent.depth, image_info->mipLevels,
        image_info->arrayLayers, image_info->samples, image_info->tiling,
        image_info->usage, image_info->sharingMode, image_info->initialLayout,
        allocation_info->flags, allocation_info->usage,
        allocation_info->requiredFlags, allocation_info->preferredFlags,
        allocation_info->memoryTypeBits, priority_bits,
        (uint64_t)allocation_info->minAlignment);
    trace->io_failed |= written < 0;
    return result;
}

void pgraph_vk_texture_image_destroy(PGRAPHVkTextureAllocationTrace *trace,
                                     VmaAllocator allocator, VkImage image,
                                     VmaAllocation allocation,
                                     uint32_t submission)
{
    if (!trace) {
        vmaDestroyImage(allocator, image, allocation);
        return;
    }
    int64_t start = g_get_monotonic_time();
    vmaDestroyImage(allocator, image, allocation);
    int64_t end = g_get_monotonic_time();
    uint64_t duration = elapsed_us(trace, start, end);
    trace->destroy_calls++;
    trace->destroy_elapsed_us += duration;
    trace->destroy_max_us = MAX(trace->destroy_max_us, duration);
    if (!reserve_record(trace)) {
        return;
    }
    int written = fprintf(
        trace->file,
        "{\"type\":\"destroy\",\"seq\":%" PRIu64 ",\"frame\":%" PRIu64
        ",\"submission\":%u"
        ",\"start_us\":%" PRId64 ",\"elapsed_us\":%" PRIu64
        ",\"image\":\"%016" PRIx64 "\",\"allocation\":\"%016" PRIxPTR "\""
        ",\"teardown\":%s}\n",
        trace->records, trace->frame, submission, start, duration,
        image_id(image), (uintptr_t)allocation, json_bool(trace->teardown));
    trace->io_failed |= written < 0;
}
