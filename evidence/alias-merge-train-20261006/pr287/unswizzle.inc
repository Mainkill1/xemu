#include "hw/xbox/nv2a/pgraph/swizzle.h"
static void run_unswizzle(PGRAPHState *pg, unsigned w, unsigned h)
{
 PGRAPHVkState *r=pg->vk_renderer_state;unsigned n=w*h;size_t bytes=n*4;
 ComputeFixtureBuffer src=create_buffer(r,bytes+256),dst=create_buffer(r,bytes+256);
 uint32_t *linear=g_new(uint32_t,n);for(unsigned i=0;i<n;i++)linear[i]=0x72561000u^i;
 swizzle_box((void*)linear,w,h,1,src.bytes,w*4,0,4);memset(dst.bytes,0xad,bytes+256);
 VkCommandBuffer cmd;VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VK_CHECK(vkAllocateCommandBuffers(r->device,&ai,&cmd));
 VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_CHECK(vkBeginCommandBuffer(cmd,&bi));
 unsigned before=r->compute.descriptor_set_index;
 assert(!pgraph_vk_unswizzle_packed_depth(pg,cmd,src.buffer,bytes-1,dst.buffer,bytes,w,h));
 assert(!pgraph_vk_unswizzle_packed_depth(pg,cmd,src.buffer,bytes,dst.buffer,bytes-1,w,h));
 assert(!pgraph_vk_unswizzle_packed_depth(pg,cmd,src.buffer,bytes,src.buffer,bytes,w,h));
 assert(!pgraph_vk_unswizzle_packed_depth(pg,cmd,src.buffer,bytes,dst.buffer,bytes,0,h));
 VkPhysicalDeviceLimits saved=r->device_props.limits;r->device_props.limits.maxComputeWorkGroupInvocations=32;
 assert(!pgraph_vk_unswizzle_packed_depth(pg,cmd,src.buffer,bytes,dst.buffer,bytes,w,h));r->device_props.limits=saved;
 assert(r->compute.descriptor_set_index==before);
 VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,NULL,0,NULL);
 assert(pgraph_vk_unswizzle_packed_depth(pg,cmd,src.buffer,bytes,dst.buffer,bytes,w,h));assert(r->compute.descriptor_set_index==before+1);
 mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
 VK_CHECK(vkEndCommandBuffer(cmd));VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};VK_CHECK(vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE));VK_CHECK(vkQueueWaitIdle(queue));
 assert(!memcmp(dst.bytes,linear,bytes));for(unsigned i=bytes;i<bytes+256;i++)assert(dst.bytes[i]==0xad);
 printf("PASS unswizzle %ux%u bytes=%zu guard=256 rejected-without-descriptor=5\n",w,h,bytes);
 vkFreeCommandBuffers(r->device,command_pool,1,&cmd);destroy_buffer(r,&src);destroy_buffer(r,&dst);g_free(linear);pgraph_vk_compute_finish_complete(r);
}
