static void run_retained_case(NV2AState *d,VkFormat format)
{
    PGRAPHState *pg=&d->pgraph;PGRAPHVkState *r=pg->vk_renderer_state;
    bool packed=format!=VK_FORMAT_D16_UNORM;
    pg->surface_scale_factor=1;
    QTAILQ_INIT(&r->surfaces);QTAILQ_INIT(&r->invalid_surfaces);
    SurfaceBinding *s=g_new0(SurfaceBinding,1);
    s->width=s->height=32;s->fmt.bytes_per_pixel=packed?4:2;
    s->pitch=32*s->fmt.bytes_per_pixel+16;s->size=32*s->pitch;
    s->host_fmt.vk_format=format;s->host_fmt.aspect=VK_IMAGE_ASPECT_DEPTH_BIT|(packed?VK_IMAGE_ASPECT_STENCIL_BIT:0);
    pg->surface_shape=(SurfaceShape){.clip_width=32,.clip_height=32,.zeta_format=packed?NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:NV097_SET_SURFACE_FORMAT_ZETA_Z16};
    pg->surface_zeta.pitch=s->pitch;pg->surface_zeta.offset=0;
    pg->surface_type=NV097_SET_SURFACE_FORMAT_TYPE_PITCH;
    r->kelvin_surface_zeta_vk_map[pg->surface_shape.zeta_format]=s->host_fmt;
    s->shape=pg->surface_shape;s->dma_len=1024*1024-1;
    s->initialized=true;s->draw_dirty=true;
    uint8_t *guest=g_malloc(s->size);memset(guest,0xab,s->size);d->vram_ptr=guest;
    VkDeviceMemory final=upload_image(r,s,false,1),scratch=upload_image(r,s,true,1);
    s->allocation=(VmaAllocation)(uintptr_t)final;s->allocation_scratch=(VmaAllocation)(uintptr_t)scratch;
    s->image_scratch_current_layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkCommandBuffer cmd=pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_transition_image_layout(pg,cmd,s->image,format,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearDepthStencilValue clear={0.25f,0x77};
    VkImageSubresourceRange range={s->host_fmt.aspect,0,1,0,1};
    vkCmdClearDepthStencilImage(cmd,s->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
    pgraph_vk_transition_image_layout(pg,cmd,s->image,format,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    pgraph_vk_end_nondraw_commands(pg,cmd);
    QTAILQ_INSERT_TAIL(&r->surfaces,s,entry);
    unsigned dirty_before=dirty_marks;
    assert(download_surface(d,s,true,true));
    assert(s->retained_guest_bytes && !s->draw_dirty && dirty_marks==dirty_before+3);
    assert(!memcmp(guest,s->retained_guest_bytes,s->size));
    for(unsigned y=0;y<32;y++){
        for(unsigned x=0;x<32;x++){
            uint32_t actual=0;memcpy(&actual,guest+y*s->pitch+x*s->fmt.bytes_per_pixel,s->fmt.bytes_per_pixel);
            if(!packed)assert(actual==16384);
            else {assert((actual&255)==0x77);uint32_t expected=format==VK_FORMAT_D24_UNORM_S8_UINT?0x400000:0x3fffff;assert((actual>>8)==expected);}
        }
        for(unsigned x=32*s->fmt.bytes_per_pixel;x<s->pitch;x++)assert(guest[y*s->pitch+x]==0xab);
    }
    SurfaceBinding target=*s;pg->surface_zeta.buffer_dirty=true;invalidate_surface(d,s);
    unsigned uploads_before=buffer_copies;VkImage retained_image=s->image;
    pgraph_vk_surface_update(d,true,false,true);
    assert(r->zeta_binding==s && s->image==retained_image && s->initialized && !s->upload_pending);
    assert(!s->retained_guest_bytes && buffer_copies==uploads_before);
    pg->surface_zeta.buffer_dirty=true;
    printf("PASS retained format=%u GPU-readback-owned-bytes/padding/eviction/full-rebind-no-upload\n",format);

    s->draw_dirty=true;assert(download_surface(d,s,true,true));invalidate_surface(d,s);
    guest[3]^=0x11;assert(!get_retained_surface(d,&target));assert(!s->retained_guest_bytes);
    assert(get_any_compatible_invalid_surface(r,&target)==s);QTAILQ_INSERT_TAIL(&r->surfaces,s,entry);
    s->draw_dirty=true;inject_guest_dirty=true;uint8_t before=guest[3];
    assert(download_surface(d,s,true,true));assert(!s->retained_guest_bytes&&s->upload_pending&&s->readback_superseded_by_guest);assert(guest[3]==before);
    printf("PASS retained format=%u partial-guest-write/reject-reuse/dirty-page-supersedes\n",format);

    s->upload_pending=false;s->readback_superseded_by_guest=false;s->draw_dirty=true;
    for(unsigned which=1;which<=2;which++){
        failpoint=which;uint8_t *saved=g_memdup2(guest,s->size);assert(!download_surface(d,s,true,true));assert(!s->retained_guest_bytes&&s->draw_dirty);assert(!memcmp(saved,guest,s->size));g_free(saved);
    }
    failpoint=PGRAPH_VK_FAILPOINT_NONE;assert(download_surface(d,s,true,true));
    printf("PASS retained format=%u failed-map/failed-invalidate/no-publication/recovery\n",format);

    /* An overlapping dirty producer must disqualify byte-equal restoration. */
    invalidate_surface(d,s);SurfaceBinding producer={.size=s->size,.draw_dirty=true};QTAILQ_INSERT_TAIL(&r->surfaces,&producer,entry);
    assert(!get_retained_surface(d,&target)&&s->retained_guest_bytes);QTAILQ_REMOVE(&r->surfaces,&producer,entry);
    unsigned destroyed_before=destroyed_images;pgraph_vk_surface_flush(d);
    assert(QTAILQ_EMPTY(&r->surfaces)&&QTAILQ_EMPTY(&r->invalid_surfaces)&&destroyed_images==destroyed_before+2);
    printf("PASS retained format=%u newer-producer-blocks/flush-releases-image-and-reference\n",format);
    /* Exercise real bounded eviction and reset cleanup beyond its keep limit. */
    destroyed_before=destroyed_images;
    for(unsigned n=0;n<12;n++) {
        SurfaceBinding *extra=g_new0(SurfaceBinding,1);*extra=target;
        extra->retained_guest_bytes=g_memdup2(guest,target.size);
        extra->image_view=VK_NULL_HANDLE;
        extra->allocation=(VmaAllocation)(uintptr_t)upload_image(r,extra,false,1);
        extra->allocation_scratch=(VmaAllocation)(uintptr_t)upload_image(r,extra,true,1);
        QTAILQ_INSERT_TAIL(&r->invalid_surfaces,extra,entry);
    }
    prune_invalid_surfaces(r,10);assert(destroyed_images==destroyed_before+4);
    unsigned kept=0;SurfaceBinding *it;QTAILQ_FOREACH(it,&r->invalid_surfaces,entry)kept++;
    assert(kept==10);pgraph_vk_surface_flush(d);
    assert(QTAILQ_EMPTY(&r->invalid_surfaces)&&destroyed_images==destroyed_before+24);
    printf("PASS retained format=%u prune12-to10/reset-flush-destroys-all\n",format);
    g_free(guest);d->vram_ptr=NULL;
}
