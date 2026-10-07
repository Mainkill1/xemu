/* Invoke the complete production blit with real GPU-owned source/destination.
 * DMA address lookup and dirty-page notification are fixture boundaries. */
static SurfaceBinding *blit_surface(NV2AState *d,VkFormat format,hwaddr addr,
                                    float depth,unsigned stencil)
{
    PGRAPHState *pg=&d->pgraph;PGRAPHVkState *r=pg->vk_renderer_state;
    SurfaceBinding *s=g_new0(SurfaceBinding,1);
    s->d=d;s->vram_addr=addr;s->width=s->height=4;s->pitch=16;s->size=64;
    s->fmt.bytes_per_pixel=s->host_fmt.host_bytes_per_pixel=4;
    s->host_fmt.vk_format=format;
    s->host_fmt.aspect=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    s->initialized=true;s->draw_dirty=true;
    s->allocation=(VmaAllocation)(uintptr_t)upload_image(r,s,false,1);
    s->allocation_scratch=(VmaAllocation)(uintptr_t)upload_image(r,s,true,1);
    s->image_scratch_current_layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkCommandBuffer cmd=pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_transition_image_layout(pg,cmd,s->image,format,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearDepthStencilValue clear={depth,stencil};
    VkImageSubresourceRange range={s->host_fmt.aspect,0,1,0,1};
    vkCmdClearDepthStencilImage(cmd,s->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
    pgraph_vk_transition_image_layout(pg,cmd,s->image,format,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    pgraph_vk_end_nondraw_commands(pg,cmd);
    QTAILQ_INSERT_TAIL(&r->surfaces,s,entry);return s;
}
static void run_blit_cases(NV2AState *d,VkFormat format)
{
    PGRAPHState *pg=&d->pgraph;PGRAPHVkState *r=pg->vk_renderer_state;
    for(unsigned mode=0;mode<3;mode++){
        QTAILQ_INIT(&r->surfaces);QTAILQ_INIT(&r->invalid_surfaces);
        r->color_binding=r->zeta_binding=NULL;
        pg->surface_color.draw_dirty=pg->surface_zeta.draw_dirty=false;
        d->vram_ptr=g_malloc0(2*MiB);memset(d->vram_ptr+0x1000,0xaa,64);memset(d->vram_ptr+0x2000,0xbb,64);
        SurfaceBinding *src=blit_surface(d,format,0x1000,0.25f,0x21);
        SurfaceBinding *dst=blit_surface(d,format,0x2000,0.75f,0x43);
        SurfaceBinding source_alias={.vram_addr=0x1000,.size=64,.swizzle=true};
        SurfaceBinding dest_alias={.vram_addr=0x2000,.size=64,.swizzle=true};
        QTAILQ_INSERT_HEAD(&r->surfaces,&source_alias,entry);
        if(mode!=1)QTAILQ_INSERT_HEAD(&r->surfaces,&dest_alias,entry);
        uint32_t sw=((format==VK_FORMAT_D24_UNORM_S8_UINT?0x400000u:0x3fffffu)<<8)|0x21;
        uint32_t dw=(0xbfffffu<<8)|0x43;
        uint8_t source[64],expected[64];
        for(unsigned i=0;i<16;i++){memcpy(source+i*4,&sw,4);memcpy(expected+i*4,&dw,4);}
        unsigned start=mode==0?1:0,extent=mode==0?2:4;
        for(unsigned y=start;y<start+extent;y++)for(unsigned x=start;x<start+extent;x++){
            unsigned off=(y*4+x)*4;
            if(mode==2)for(unsigned c=0;c<3;c++)expected[off+c]=(source[off+c]+expected[off+c])/2;
            else memcpy(expected+off,source+off,4);
        }
        pg->context_surfaces_2d=(ContextSurfaces2DState){.object_instance=7,
            .color_format=NV062_SET_COLOR_FORMAT_LE_A8R8G8B8,
            .source_offset=0x1000,.dest_offset=0x2000,.source_pitch=16,.dest_pitch=16};
        pg->image_blit=(ImageBlitState){.context_surfaces=7,
            .operation=mode==2?NV09F_SET_OPERATION_BLEND_AND:NV09F_SET_OPERATION_SRCCOPY,
            .in_x=start,.in_y=start,.out_x=start,.out_y=start,.width=extent,.height=extent};
        pg->beta.beta=0x3fc00000;
        unsigned reads=image_readbacks;
        pgraph_vk_image_blit(d);
        assert(!memcmp(d->vram_ptr+0x1000,source,64));
        assert(!memcmp(d->vram_ptr+0x2000,expected,64));
        assert(image_readbacks-reads==(mode==1?1:2));
        assert(!src->draw_dirty && dst->upload_pending && !dst->draw_dirty);
        if(mode!=1)assert(dest_alias.upload_pending&&!dest_alias.draw_dirty);
        QTAILQ_REMOVE(&r->surfaces,&source_alias,entry);
        if(mode!=1)QTAILQ_REMOVE(&r->surfaces,&dest_alias,entry);
        pgraph_vk_surface_flush(d);g_free(d->vram_ptr);d->vram_ptr=NULL;
        pgraph_vk_compute_finish_complete(r);
        printf("PASS native blit-consumer format=%u mode=%s GPU-readbacks=%u all64bytes=exact\n",
            format,mode==0?"partial-copy":mode==1?"full-copy":"full-blend",image_readbacks-reads);
    }
}
