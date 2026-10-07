/* Actual GPU commands reference the retired images until a real queue wait.
 * Submission is the fixture boundary; production ownership and pruning run. */
static void run_retirement_case(NV2AState *d, VkFormat format)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    SurfaceBinding producer = { .vram_addr = 4096 };
    SurfaceBinding *views[11];
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INIT(&r->invalid_surfaces);
    QTAILQ_INSERT_TAIL(&r->surfaces, &producer, entry);
    r->perf.enabled = true;
    r->perf.depth_alias_pending_retirements_peak = 0;
    r->perf.depth_alias_bound_drains = 0;
    unsigned initial_finishes = main_finishes;
    unsigned initial_destroyed = destroyed_images;
    pending_main = pgraph_vk_begin_nondraw_commands(pg);
    r->in_command_buffer = true;
    for (unsigned i = 0; i < 11; i++) {
        SurfaceBinding *v = views[i] = g_new0(SurfaceBinding, 1);
        v->width = v->height = 4;
        v->vram_addr = producer.vram_addr;
        v->swizzle = true;
        v->host_fmt.vk_format = format;
        v->host_fmt.aspect = VK_IMAGE_ASPECT_DEPTH_BIT |
                             VK_IMAGE_ASPECT_STENCIL_BIT;
        v->allocation = (VmaAllocation)(uintptr_t)create_depth_image(r, v);
        QTAILQ_INSERT_TAIL(&r->surfaces, v, entry);
        pgraph_vk_transition_image_layout(pg, pending_main, v->image, format,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearDepthStencilValue clear = { 0.5f, 0x33 + i };
        VkImageSubresourceRange range = { v->host_fmt.aspect, 0, 1, 0, 1 };
        vkCmdClearDepthStencilImage(pending_main, v->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        pgraph_vk_transition_image_layout(pg, pending_main, v->image, format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        pgraph_vk_surface_invalidate_depth_views(pg, &producer);
        assert(r->pending_alias_retirements <= 10);
        if (i < 10) {
            assert(v->retirement_pending);
            prune_invalid_surfaces(r, 0);
            assert(destroyed_images == initial_destroyed);
            assert(main_finishes == initial_finishes);
        }
    }
    assert(!pending_main && !r->in_command_buffer);
    assert(main_finishes == initial_finishes + 1);
    assert(r->perf.depth_alias_pending_retirements_peak == 10);
    assert(r->perf.depth_alias_bound_drains == 1);
    assert(!r->pending_alias_retirements);
    ComputeFixtureBuffer readback = create_buffer(r, 4096);
    unsigned stencil_offset = ROUND_UP(16 * 4,
        r->device_props.limits.minStorageBufferOffsetAlignment);
    for (unsigned i = 0; i < 11; i++) {
        assert(!views[i]->retirement_pending);
        copy_depth_image(pg, views[i], &readback, false,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        for (unsigned j = 0; j < 16; j++) {
            uint32_t depth;
            memcpy(&depth, readback.bytes + j * 4, 4);
            assert(depth == (format == VK_FORMAT_D32_SFLOAT_S8_UINT ?
                            float_bits(0.5f) : 0x800000));
            assert(readback.bytes[stencil_offset + j] == 0x33 + i);
        }
    }
    destroy_buffer(r, &readback);
    QTAILQ_REMOVE(&r->surfaces, &producer, entry);
    pgraph_vk_surface_flush(d);
    assert(QTAILQ_EMPTY(&r->surfaces) && QTAILQ_EMPTY(&r->invalid_surfaces));
    assert(destroyed_images == initial_destroyed + 11);
    r->perf.enabled = false;
    printf("PASS native retirement format=%u pending10/prune-protected/bound-drain11/queued-image-oracle/flush\n", format);
}
