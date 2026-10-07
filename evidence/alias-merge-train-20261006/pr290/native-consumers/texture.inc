void fixture_copy_zeta_texture(PGRAPHState *pg, SurfaceBinding *surface,
                               TextureBinding *texture);
static void check_texture_consumer(PGRAPHState *pg, SurfaceBinding *view,
                                    const uint8_t *source, unsigned source_stencil,
                                    VkFormat depth_format)
{
    PGRAPHVkState *r=pg->vk_renderer_state;
    SurfaceBinding color={.width=view->width,.height=view->height,.color=true};
    color.host_fmt.vk_format=VK_FORMAT_B8G8R8A8_UNORM;
    VkDeviceMemory memory=upload_image(r,&color,false,1);
    TextureBinding texture={.image=color.image,.current_layout=VK_IMAGE_LAYOUT_UNDEFINED};
    texture.key.state.color_format=NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8;
    fixture_copy_zeta_texture(pg,view,&texture);
    assert(texture.current_layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    unsigned pixels=view->width*view->height;
    ComputeFixtureBuffer out=create_buffer(r,pixels*4);
    VkCommandBuffer cmd=pgraph_vk_begin_nondraw_commands(pg);
    /* Diagnostic readback is outside the production texture consumer. */
    VkImageMemoryBarrier image_barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout=texture.current_layout,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=texture.image,.subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1}};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&image_barrier);
    VkBufferImageCopy copy={.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},.imageExtent={view->width,view->height,1}};
    vkCmdCopyImageToBuffer(cmd,texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,out.buffer,1,&copy);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    pgraph_vk_end_nondraw_commands(pg,cmd);
    for(unsigned i=0;i<pixels;i++){
        unsigned input=quadrant_index(i%view->width,i/view->width,view->width);
        uint32_t depth,actual;memcpy(&depth,source+input*4,4);
        if(depth_format==VK_FORMAT_D32_SFLOAT_S8_UINT){float f;memcpy(&f,&depth,4);depth=(uint32_t)(f*(float)0xffffff);}
        memcpy(&actual,out.bytes+i*4,4);
        assert(actual==((depth<<8)|source[source_stencil+input]));
    }
    destroy_buffer(r,&out);vkDestroyImage(r->device,color.image,NULL);vkFreeMemory(r->device,memory,NULL);
    printf("PASS native texture-consumer format=%u pixels=%u\n",depth_format,pixels);
}
