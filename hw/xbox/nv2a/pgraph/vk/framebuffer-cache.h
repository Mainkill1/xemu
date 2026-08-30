/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_FRAMEBUFFER_CACHE_H
#define HW_XBOX_NV2A_PGRAPH_VK_FRAMEBUFFER_CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#define PGRAPH_VK_FRAMEBUFFER_CACHE_SIZE 50

typedef struct PGRAPHVkFramebufferKey {
    VkRenderPass render_pass;
    VkImageView attachments[2];
    uint32_t attachment_count;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
} PGRAPHVkFramebufferKey;

static inline bool pgraph_vk_framebuffer_key_equal(
    const PGRAPHVkFramebufferKey *a, const PGRAPHVkFramebufferKey *b)
{
    if (a->render_pass != b->render_pass ||
        a->attachment_count != b->attachment_count || a->width != b->width ||
        a->height != b->height || a->layers != b->layers) {
        return false;
    }
    for (uint32_t i = 0; i < a->attachment_count; i++) {
        if (a->attachments[i] != b->attachments[i]) {
            return false;
        }
    }
    return true;
}

static inline bool pgraph_vk_framebuffer_key_references_view(
    const PGRAPHVkFramebufferKey *key, VkImageView view)
{
    for (uint32_t i = 0; i < key->attachment_count; i++) {
        if (key->attachments[i] == view) {
            return true;
        }
    }
    return false;
}

#endif
