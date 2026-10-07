/* Reuses native-compute.c's device, buffers and shader compiler. Only command
 * submission and preallocated scratch capacity are fixture adapters. The
 * converter, image barriers, descriptor updates and all shaders are production. */
static ComputeFixtureBuffer alias_scratch[2];

void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize bytes)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(r->storage_buffers[index].buffer_size >= bytes);
}

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(r->device, &allocation, &cmd));
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    return cmd;
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(r->device, command_pool, 1, &cmd);
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    VK_CHECK(vkQueueWaitIdle(queue));
    pgraph_vk_compute_finish_complete(pg->vk_renderer_state);
}

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

    /* Two independently recognizable generations, including scratch repeat. */
    for (unsigned generation = 0; generation < 2; generation++) {
        for (unsigned i = 0; i < producer_pixels; i++) {
            uint32_t depth = format == VK_FORMAT_D32_SFLOAT_S8_UINT ?
                float_bits((float)((i + generation) % 5) * 0.25f) :
                ((i * 1009u + generation * 65537u) ^ (i >> 2)) & 0xffffff;
            memcpy(source.bytes + i * 4, &depth, 4);
            source.bytes[source_stencil + i] = (i * 31u + generation * 7u) & 0xff;
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
        assert(pgraph_vk_convert_depth_alias(pg, &producer, &view));
        assert(r->compute.descriptor_set_index == descriptors + 3);
        copy_depth_image(pg, &view, &target, false, source_layout);
        unsigned zero_canonicalized = 0;
        for (unsigned i = 0; i < view_pixels; i++) {
            unsigned input = quadrant_index(i % view_edge, i / view_edge, view_edge);
            uint32_t expected, actual;
            uint32_t shader_expected, shader_actual;
            memcpy(&shader_expected, oracle_upload.bytes + i * 4, 4);
            memcpy(&shader_actual, alias_scratch[0].bytes + i * 4, 4);
            assert(shader_actual == shader_expected);
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
        printf("PASS native alias format=%u %ux%u->%ux%u generation=%u repeat=%u pixels=%u zeroCanonicalized=%u\n",
                format, producer_width, producer_height, view_edge, view_edge,
                generation, repeat, view_pixels, zero_canonicalized);
        }
        pgraph_vk_compute_finish_complete(r);
    }
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
    }
}
