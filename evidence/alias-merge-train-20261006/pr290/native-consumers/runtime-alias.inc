static uint32_t float_bits(float f) { uint32_t u; memcpy(&u, &f, sizeof(u)); return u; }
static ComputeFixtureBuffer alias_scratch[2];
static VkDeviceMemory create_depth_image(PGRAPHVkState *r, SurfaceBinding *s)
{
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = s->host_fmt.vk_format,
        .extent = { s->width, s->height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(r->device, &info, NULL, &s->image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(r->device, s->image, &requirements);
    unsigned type = ctz32(requirements.memoryTypeBits);
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type,
    };
    VkDeviceMemory memory;
    VK_CHECK(vkAllocateMemory(r->device, &allocation, NULL, &memory));
    VK_CHECK(vkBindImageMemory(r->device, s->image, memory, 0));
    return memory;
}

static void copy_depth_image(PGRAPHState *pg, SurfaceBinding *s,
                              ComputeFixtureBuffer *b, bool upload,
                              VkImageLayout initial_layout)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    VkImageLayout transfer = upload ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL :
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pgraph_vk_transition_image_layout(pg, cmd, s->image, s->host_fmt.vk_format,
                                      initial_layout, transfer);
    VkBufferImageCopy regions[2] = {
        { .imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
          .imageSubresource.layerCount = 1,
          .imageExtent = { s->width, s->height, 1 } },
        { .bufferOffset = ROUND_UP(s->width * s->height * 4,
              r->device_props.limits.minStorageBufferOffsetAlignment),
          .imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
          .imageSubresource.layerCount = 1,
          .imageExtent = { s->width, s->height, 1 } },
    };
    if (upload) {
        vkCmdCopyBufferToImage(cmd, b->buffer, s->image, transfer, 2, regions);
    } else {
        vkCmdCopyImageToBuffer(cmd, s->image, transfer, b->buffer, 2, regions);
        VkMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_HOST_BIT, 0,
                              1, &barrier, 0, NULL, 0, NULL);
    }
    pgraph_vk_transition_image_layout(pg, cmd, s->image, s->host_fmt.vk_format,
        transfer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    pgraph_vk_end_nondraw_commands(pg, cmd);
}

static unsigned quadrant_index(unsigned x, unsigned y, unsigned edge)
{
    unsigned index = 0;
    for (unsigned half = edge / 2; half; half /= 2) {
        index += ((x & half ? 1 : 0) + (y & half ? 2 : 0)) * half * half;
    }
    return index;
}

#include "texture.inc"

static void run_alias_case(PGRAPHState *pg, VkFormat format,
                           unsigned producer_width, unsigned producer_height,
                           unsigned view_edge)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    SurfaceBinding producer = {
        .width = producer_width, .height = producer_height,
        .host_fmt.vk_format = format,
        .lifetime_id = 1, .initialized = true, .draw_dirty = true,
    };
    SurfaceBinding view = {
        .width = view_edge, .height = view_edge,
        .host_fmt.vk_format = format, .swizzle = true,
    };
    VkImageFormatProperties format_properties;
    VkResult format_result = vkGetPhysicalDeviceImageFormatProperties(
        physical, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        0, &format_properties);
    printf("image format=%u admission=%d\n", format, format_result);
    if (format_result != VK_SUCCESS) {
        printf("SKIP unsupported depth image format=%u\n", format);
        return;
    }
    pg->surface_scale_factor = 1;
    unsigned producer_pixels = producer_width * producer_height;
    unsigned view_pixels = view_edge * view_edge;
    size_t source_stencil = ROUND_UP(producer_pixels * 4,
        r->device_props.limits.minStorageBufferOffsetAlignment);
    size_t target_stencil = ROUND_UP(view_pixels * 4,
        r->device_props.limits.minStorageBufferOffsetAlignment);
    ComputeFixtureBuffer source = create_buffer(r, source_stencil + producer_pixels);
    ComputeFixtureBuffer target = create_buffer(r, target_stencil + view_pixels);
    ComputeFixtureBuffer source_readback = create_buffer(r, source_stencil + producer_pixels);
    ComputeFixtureBuffer oracle_upload = create_buffer(r, target_stencil + view_pixels);
    ComputeFixtureBuffer oracle_readback = create_buffer(r, target_stencil + view_pixels);
    SurfaceBinding reference = view;
    VkDeviceMemory reference_memory = create_depth_image(r, &reference);
    VkImageLayout reference_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDeviceMemory producer_memory = create_depth_image(r, &producer);
    VkDeviceMemory view_memory = create_depth_image(r, &view);
    VkImageLayout source_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    copy_depth_image(pg, &view, &target, true, VK_IMAGE_LAYOUT_UNDEFINED);

    NV2AState *d = container_of(pg, NV2AState, pgraph);
    QTAILQ_INIT(&r->surfaces);QTAILQ_INIT(&r->invalid_surfaces);
    producer.d=d;view.d=d;
    producer.shape.zeta_format = NV097_SET_SURFACE_FORMAT_ZETA_Z24S8;
    producer.fmt.bytes_per_pixel = 4;
    producer.pitch = producer_width * 4;
    producer.size = producer.pitch * producer_height;
    producer.dma_len = 2 * MiB - 1;
    producer.host_fmt.aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    view.host_fmt.aspect=producer.host_fmt.aspect;view.host_fmt.host_bytes_per_pixel=4;
    view.shape = producer.shape;view.fmt=producer.fmt;view.pitch=producer.pitch;
    view.size=view.pitch*view.height;view.dma_len=producer.dma_len;
    pg->surface_shape.anti_aliasing=NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1;
    QTAILQ_INSERT_TAIL(&r->surfaces,&producer,entry);
    QTAILQ_INSERT_TAIL(&r->surfaces,&view,entry);
    assert(pgraph_vk_find_depth_alias_producer(d,&view,&view)==&producer);
    /* Two independently recognizable generations, including runtime reuse. */
    for (unsigned generation = 0; generation < 2; generation++) {
        for (unsigned i = 0; i < producer_pixels; i++) {
            uint32_t depth = format == VK_FORMAT_D32_SFLOAT_S8_UINT ?
                float_bits((float)((i + generation) % 5) * 0.25f) :
                ((i * 1009u + generation * 65537u) ^ (i >> 2)) & 0xffffff;
            memcpy(source.bytes + i * 4, &depth, 4);
            source.bytes[source_stencil + i] = (i * 31u + generation * 7u) & 0xff;
        }
        if(generation) {
            pgraph_vk_surface_invalidate_depth_views(pg,&producer);
            assert(!view.retirement_pending);
            bool live=false,invalid=false;SurfaceBinding *entry;
            QTAILQ_FOREACH(entry,&r->surfaces,entry)live|=entry==&view;
            QTAILQ_FOREACH(entry,&r->invalid_surfaces,entry)invalid|=entry==&view;
            assert(!live && invalid);
            QTAILQ_REMOVE(&r->invalid_surfaces,&view,entry);
            QTAILQ_INSERT_TAIL(&r->surfaces,&view,entry);
        }
        copy_depth_image(pg, &producer, &source, true, source_layout);
        source_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        copy_depth_image(pg, &producer, &source_readback, false, source_layout);
        for (unsigned i = 0; i < producer_pixels; i++) {
            uint32_t expected, actual;
            memcpy(&expected, source.bytes + i * 4, 4);
            memcpy(&actual, source_readback.bytes + i * 4, 4);
            if (actual != expected || source.bytes[source_stencil + i] !=
                                      source_readback.bytes[source_stencil + i]) {
                fprintf(stderr, "producer roundtrip mismatch format=%u generation=%u pixel=%u "
                        "depth=%08x expected=%08x stencil=%u expected=%u\n",
                        format, generation, i, actual, expected,
                        source_readback.bytes[source_stencil + i], source.bytes[source_stencil + i]);
                abort();
            }
        }
        printf("PASS producer roundtrip format=%u generation=%u pixels=%u\n",
                format, generation, producer_pixels);
        /* Independent CPU mapping -> reference image transfer. This preserves
         * the device's depth transfer representation, including denormal zero. */
        for (unsigned i = 0; i < view_pixels; i++) {
            unsigned input = quadrant_index(i % view_edge, i / view_edge, view_edge);
            uint32_t expected;
            memcpy(&expected, source.bytes + input * 4, 4);
            if (format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
                float depth;
                memcpy(&depth, &expected, 4);
                uint32_t guest = (uint32_t)(depth * (float)0xffffff);
                expected = float_bits((float)guest / 16777216.0f) + 1;
            }
            memcpy(oracle_upload.bytes + i * 4, &expected, 4);
            oracle_upload.bytes[target_stencil + i] = source.bytes[source_stencil + input];
        }
        copy_depth_image(pg, &reference, &oracle_upload, true, reference_layout);
        reference_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        copy_depth_image(pg, &reference, &oracle_readback, false, reference_layout);
        for (unsigned repeat = 0; repeat < 2; repeat++) {
        producer.draw_time = ++pg->draw_time;
        unsigned descriptors = r->compute.descriptor_set_index;
        assert(update_depth_alias_view(pg, &producer, &view));
        assert(r->compute.descriptor_set_index == descriptors + (repeat ? 0 : 3));
        copy_depth_image(pg, &view, &target, false, source_layout);
        unsigned zero_canonicalized = 0;
        for (unsigned i = 0; i < view_pixels; i++) {
            unsigned input = quadrant_index(i % view_edge, i / view_edge, view_edge);
            uint32_t expected, actual;
            uint32_t shader_expected, shader_actual;
            memcpy(&shader_expected, oracle_upload.bytes + i * 4, 4);
            memcpy(&shader_actual, alias_scratch[0].bytes + i * 4, 4);
            /* Texture consumption reuses scratch after conversion. A cache
             * hit is checked from the image, not stale temporary storage. */
            if (!repeat) { assert(shader_actual == shader_expected); }
            memcpy(&expected, oracle_readback.bytes + i * 4, 4);
            if (expected != shader_expected) {
                assert(format == VK_FORMAT_D32_SFLOAT_S8_UINT &&
                       expected == 0 && shader_expected == 1);
                zero_canonicalized++;
            }
            memcpy(&actual, target.bytes + i * 4, 4);
            if (format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
                float source_depth, image_depth;
                memcpy(&source_depth, source.bytes + input * 4, 4);
                memcpy(&image_depth, &actual, 4);
                assert((uint32_t)(image_depth * (float)0xffffff) ==
                       (uint32_t)(source_depth * (float)0xffffff));
            }
            if (actual != expected || target.bytes[target_stencil + i] !=
                                      source.bytes[source_stencil + input]) {
                fprintf(stderr, "alias mismatch format=%u generation=%u pixel=%u "
                        "depth=%08x expected=%08x stencil=%u expected=%u\n",
                        format, generation, i, actual, expected,
                        target.bytes[target_stencil + i],
                        source.bytes[source_stencil + input]);
                for (unsigned j = 0; j < 16; j++) {
                    uint32_t src, split, packed;
                    memcpy(&src, source.bytes + j * 4, 4);
                    memcpy(&split, alias_scratch[1].bytes + j * 4, 4);
                    memcpy(&packed, alias_scratch[0].bytes + j * 4, 4);
                    fprintf(stderr, "pixel=%u src=%08x split=%08x packed=%08x\n", j, src, split, packed);
                }
                abort();
            }
        }
        check_texture_consumer(pg,&view,source.bytes,source_stencil,format);
        printf("PASS native alias format=%u %ux%u->%ux%u generation=%u repeat=%u pixels=%u zeroCanonicalized=%u\n",
                format, producer_width, producer_height, view_edge, view_edge,
                generation, repeat, view_pixels, zero_canonicalized);
        }
        pgraph_vk_compute_finish_complete(r);
    }
    /* Failed recording must clear provenance and leave descriptors untouched. */
    producer.depth_write_generation++;
    uint32_t saved_range=r->device_props.limits.maxStorageBufferRange;
    r->device_props.limits.maxStorageBufferRange=0;
    unsigned descriptors=r->compute.descriptor_set_index;
    assert(!update_depth_alias_view(pg,&producer,&view));
    assert(view.derived_from_lifetime_id==0);
    assert(r->compute.descriptor_set_index==descriptors);
    r->device_props.limits.maxStorageBufferRange=saved_range;
    assert(update_depth_alias_view(pg,&producer,&view));
    printf("PASS runtime admission/rewrite/reuse/failure-recovery format=%u width=%u\n",format,producer_width);
    /* Exercise the CPU callback and real download publication. PFIFO dispatch
     * is synchronous in this fixture; memory callbacks are invoked directly. */
    d->vram_ptr=g_malloc0(2*MiB);
    producer.allocation_scratch=(VmaAllocation)(uintptr_t)upload_image(r,&producer,true,1);
    producer.image_scratch_current_layout=VK_IMAGE_LAYOUT_UNDEFINED;
    producer.draw_dirty=true;
    surface_access_callback(&producer,NULL,0,4,false);
    assert(!producer.draw_dirty && !producer.download_pending);
    for(unsigned i=0;i<producer_pixels;i++) {
        uint32_t depth,guest;
        memcpy(&depth,source.bytes+i*4,4);
        if(format==VK_FORMAT_D32_SFLOAT_S8_UINT){float f;memcpy(&f,&depth,4);depth=(uint32_t)(f*(float)0xffffff);}
        memcpy(&guest,d->vram_ptr+i*4,4);
        assert(guest==((depth<<8)|source.bytes[source_stencil+i]));
    }
    uint8_t *producer_expected=g_memdup2(d->vram_ptr,producer.size);
    producer.draw_dirty=true;
    surface_access_callback(&view,NULL,5,1,true);
    surface_access_callback(&producer,NULL,5,1,true);
    assert(view.upload_pending && producer.upload_pending && !producer.draw_dirty);
    uint8_t *saved=g_memdup2(d->vram_ptr,producer.size);
    d->vram_ptr[5]^=0x5a;saved[5]^=0x5a;
    assert(!pgraph_vk_find_depth_alias_producer(d,&view,&view));
    producer.upload_pending=false;producer.draw_dirty=true;inject_guest_dirty=true;
    assert(pgraph_vk_download_surfaces_in_range_if_dirty(pg,0,producer.size));
    assert(!memcmp(saved,d->vram_ptr,producer.size));g_free(saved);
    assert(producer.upload_pending && producer.readback_superseded_by_guest);
    /* A newly rendered producer must publish before writable alias admission. */
    producer.upload_pending=false;producer.readback_superseded_by_guest=false;
    producer.draw_dirty=true;
    view.upload_pending=false;
    retire_depth_alias_for_write(d,producer.vram_addr);
    assert(view.upload_pending && !producer.draw_dirty);
    assert(!memcmp(producer_expected,d->vram_ptr,producer.size));g_free(producer_expected);
    assert(pgraph_vk_surface_get(d,0)==&view);
    printf("PASS runtime CPU-read/partial-write/dirty-page/writable-transition format=%u width=%u\n",format,producer_width);
    QTAILQ_REMOVE(&r->surfaces,&view,entry);
    QTAILQ_REMOVE(&r->invalid_surfaces,&producer,entry);
    vkDestroyImage(r->device,producer.image_scratch,NULL);
    vkFreeMemory(r->device,(VkDeviceMemory)(uintptr_t)producer.allocation_scratch,NULL);
    g_free(producer.retained_guest_bytes);g_free(d->vram_ptr);d->vram_ptr=NULL;
    pgraph_vk_compute_finish_complete(r);
    vkDestroyImage(r->device, reference.image, NULL);
    vkFreeMemory(r->device, reference_memory, NULL);
    destroy_buffer(r, &oracle_upload);
    destroy_buffer(r, &oracle_readback);
    vkDestroyImage(r->device, producer.image, NULL);
    vkDestroyImage(r->device, view.image, NULL);
    vkFreeMemory(r->device, producer_memory, NULL);
    vkFreeMemory(r->device, view_memory, NULL);
    destroy_buffer(r, &source_readback);
    destroy_buffer(r, &source);
    destroy_buffer(r, &target);
}

static void run_native_alias_cases(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const int indices[] = { BUFFER_COMPUTE_SRC, BUFFER_COMPUTE_DST };
    for (unsigned i = 0; i < 2; i++) {
        alias_scratch[i] = create_buffer(r, 2 * MiB);
        r->storage_buffers[indices[i]].buffer = alias_scratch[i].buffer;
        r->storage_buffers[indices[i]].allocation=(VmaAllocation)&alias_scratch[i];
        r->storage_buffers[indices[i]].buffer_size = 2 * MiB;
    }
    for (unsigned i = 0; i < 2; i++) {
        VkFormat format = i ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D24_UNORM_S8_UINT;
        run_alias_case(pg, format, 64, 16, 4);
        run_alias_case(pg, format, 640, 480, 32);
        run_alias_case(pg, format, 34, 64, 32);
        run_alias_case(pg, format, 35, 64, 32);
    }
    for (unsigned i = 0; i < 2; i++) {
        destroy_buffer(r, &alias_scratch[i]);
        memset(&r->storage_buffers[indices[i]],0,sizeof(r->storage_buffers[0]));
    }
}
