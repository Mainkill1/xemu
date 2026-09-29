/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/vk/shader-browser-input-pressure.h"
#include <stdio.h>

int main(void)
{
    /* Repeated 50 MiB draws previously exhausted staging before the frame
     * fence, despite final immutable deduplication. Retiring at completed draw
     * boundaries must preserve every draw within a 256 MiB staging pool. */
    const size_t draw_bytes = 50U * 1024U * 1024U;
    size_t pending = 0, retired = 0;
    for (unsigned draw = 0; draw < 32; ++draw) {
        if (pending + draw_bytes > 256U * 1024U * 1024U) {
            fprintf(stderr, "Readback staging exhausted at draw %u\n", draw);
            return 1;
        }
        pending += draw_bytes;
        if (pgraph_vk_input_should_drain(pending, 0, false, true, false, 0)) {
            retired += pending;
            pending = 0;
        }
    }
    /* Sharing one physical snapshot must not suppress retirement of the
     * independent future CPU payload reservations for its consumers. */
    const size_t shared_staging = 4U * 1024U * 1024U;
    const size_t consumer_payload = 128U * 1024U * 1024U;
    if (!pgraph_vk_input_should_drain(shared_staging, consumer_payload, false,
                                      true, false, 0)) {
        fprintf(stderr, "Shared readbacks did not drain CPU reservations\n");
        return 1;
    }
    if (!pgraph_vk_input_should_drain(shared_staging, shared_staging, true,
                                      true, false, 0) ||
        pgraph_vk_input_should_drain(shared_staging, shared_staging, true, true,
                                     true, 0) ||
        pgraph_vk_input_should_drain(shared_staging, shared_staging, true, true,
                                     false, 1) ||
        pgraph_vk_input_should_drain(0, 0, true, true, false, 0)) {
        fprintf(stderr, "Recorder pressure drained at an unsafe boundary\n");
        return 1;
    }
    if (retired + pending != 1600U * 1024U * 1024U || !retired ||
        pgraph_vk_input_should_drain(0, 0, false, true, false, 0) ||
        pgraph_vk_input_should_drain(127U * 1024U * 1024U, 0, false, true,
                                     false, 0) ||
        !pgraph_vk_input_should_drain(128U * 1024U * 1024U, 0, false, true,
                                      false, 0) ||
        pgraph_vk_input_should_drain(192U * 1024U * 1024U, 0, true, false,
                                     false, 0) ||
        pgraph_vk_input_should_drain(192U * 1024U * 1024U, 0, true, true, true,
                                     0) ||
        pgraph_vk_input_should_drain(192U * 1024U * 1024U, 0, true, true, false,
                                     1)) {
        fprintf(stderr, "Readbacks drained at an unsafe boundary\n");
        return 1;
    }
    puts("Vulkan bounded capture pressure passed");
    return 0;
}
