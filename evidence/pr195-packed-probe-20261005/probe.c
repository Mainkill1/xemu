#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "current.h"
#include "packed.h"
#include "main.h"

static MCPXAPUADPCMDecodeTable current_table;
static PackedTable packed_table;
static uint8_t blocks[2048][72];
static int16_t outputs[2048][130];

__attribute__((noinline)) static int decode(unsigned mode, int16_t *out,
    const uint8_t *in, size_t size, int channels)
{
    if (mode == 0) {
        return main_adpcm_decode_block(out, in, size, channels);
    }
    if (mode == 1) {
        return adpcm_decode_block(&current_table, out, in, size, channels);
    }
    return packed_decode_block(&packed_table, out, in, size, channels);
}

static uint32_t random_word(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static uint64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static uint64_t digest(void)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (unsigned block = 0; block < 2048; block++) {
        for (unsigned i = 0; i < 130; i++) {
            hash ^= (uint16_t)outputs[block][i];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

int main(int argc, char **argv)
{
    mcpx_apu_adpcm_decode_table_init(&current_table);
    packed_table_init(&packed_table);
    uint32_t seed = 0x12345678;
    for (unsigned j = 0; j < 2048; j++) {
        for (unsigned i = 0; i < 72; i++) {
            blocks[j][i] = random_word(&seed);
        }
        blocks[j][2] = j % 89;
        blocks[j][3] = 0;
        blocks[j][6] = (j * 17) % 89;
        blocks[j][7] = 0;
    }
    if (argc == 1) {
        unsigned checked = 0;
        for (unsigned index = 0; index < 89; index++) {
            for (unsigned nibble = 0; nibble < 16; nibble++) {
                uint32_t word = packed_table.transition[index][nibble];
                if ((int32_t)(word >> 7) - 65536 !=
                        current_table.delta[index][nibble] ||
                    (word & 127) != current_table.next_index[index][nibble]) {
                    return 1;
                }
            }
        }
        for (int channels = 1; channels <= 2; channels++) {
            for (unsigned i = 0; i < 2048; i++) {
                for (unsigned chunks = 0; chunks <= 8; chunks++) {
                    size_t size = channels * (4 + chunks * 4);
                    int16_t original[130] = { 0 };
                    int16_t result[130] = { 0 };
                    int count = decode(0, original, blocks[i], size, channels);
                    for (unsigned mode = 1; mode <= 2; mode++) {
                        int other = decode(mode, result, blocks[i], size, channels);
                        if (other != count ||
                            memcmp(original, result,
                                   count * channels * sizeof(int16_t))) {
                            return 1;
                        }
                        checked++;
                    }
                }
            }
        }
        printf("equivalent: 1424 transitions and %u decoder cases\n", checked);
        return 0;
    }
    if (argc != 4) {
        return 2;
    }
    unsigned mode = strtoul(argv[1], NULL, 0);
    int channels = atoi(argv[2]);
    uint64_t iterations = strtoull(argv[3], NULL, 0);
    uint64_t count = 0;
    uint64_t start = now();
    for (uint64_t i = 0; i < iterations; i++) {
        unsigned j = i & 2047;
        count += decode(mode, outputs[j], blocks[j], channels * 36, channels);
    }
    uint64_t elapsed = now() - start;
    printf("{\"mode\":%u,\"channels\":%d,\"elapsedNs\":%" PRIu64
           ",\"samples\":%" PRIu64 ",\"checksum\":%" PRIu64
           ",\"nsPerBlock\":%.6f}\n",
           mode, channels, elapsed, count, digest(), (double)elapsed / iterations);
    return 0;
}
