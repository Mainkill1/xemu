#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "baseline-workgroup.h"
#undef HW_XBOX_NV2A_PGRAPH_VK_COMPUTE_WORKGROUP_H
#include <hw/xbox/nv2a/pgraph/vk/compute-workgroup.h>

__attribute__((noinline)) static uint32_t current_call(uint64_t n,
                                                      uint32_t x, uint32_t inv)
{
    return baseline_workgroup_size(n, x, inv);
}
__attribute__((noinline)) static uint32_t proposed_call(uint64_t n,
                                                       uint32_t x, uint32_t inv)
{
    return pgraph_vk_compute_workgroup_size(n, x, inv);
}
static uint64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
int main(int argc, char **argv)
{
    if (argc == 1) {
        uint32_t limits[] = {0, 1, 2, 3, 7, 63, 127, 128, 192, 256,
                             511, 512, 1023, 1024, 2048, UINT32_MAX};
        uint64_t checks = 0;
        for (unsigned a = 0; a < sizeof(limits) / sizeof(limits[0]); a++) {
            for (unsigned b = 0; b < sizeof(limits) / sizeof(limits[0]); b++) {
                for (uint64_t n = 0; n <= 4096; n++) {
                    uint64_t values[] = {n, (UINT64_C(1) << 40) + n,
                                         UINT64_MAX - n};
                    for (unsigned k = 0; k < 3; k++) {
                        if (current_call(values[k], limits[a], limits[b]) !=
                            proposed_call(values[k], limits[a], limits[b])) {
                            return 1;
                        }
                        checks++;
                    }
                }
            }
        }
        printf("equivalent: %" PRIu64 " cases\n", checks);
        return 0;
    }
    if (argc != 6) {
        return 2;
    }
    uint32_t mode = strtoul(argv[1], NULL, 0);
    uint64_t n = strtoull(argv[2], NULL, 0);
    uint32_t x = strtoul(argv[3], NULL, 0);
    uint32_t inv = strtoul(argv[4], NULL, 0);
    uint64_t iterations = strtoull(argv[5], NULL, 0);
    uint64_t sum = 0, start = now();
    for (uint64_t i = 0; i < iterations; i++) {
        uint64_t input = n + ((i & 1) * 1024);
        sum += mode ? proposed_call(input, x, inv) : current_call(input, x, inv);
    }
    uint64_t elapsed = now() - start;
    printf("{\"mode\":%u,\"elapsedNs\":%" PRIu64 ",\"checksum\":%" PRIu64
           ",\"nsPerCall\":%.6f}\n", mode, elapsed, sum,
           (double)elapsed / iterations);
    return 0;
}
