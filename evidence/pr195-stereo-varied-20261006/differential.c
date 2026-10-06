#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define adpcm_decode_block scalar_decode_block
#include "reference-adpcm.h"
#undef adpcm_decode_block
#undef ADPCM_DECODE_H
#include "hw/xbox/mcpx/apu/vp/adpcm.h"

int main(void)
{
    MCPXAPUADPCMDecodeTable table, original;
    unsigned cases = 0;
    mcpx_apu_adpcm_decode_table_init(&table);
    original = table;
    for (int channels = 1; channels <= 2; channels++) {
        for (int index = 0; index < 89; index++) {
            for (int chunks = 1; chunks <= 16; chunks++) {
                for (int seed = 0; seed < 4; seed++) {
                    uint8_t encoded[136];
                    int16_t reference[258], candidate[258];
                    size_t bytes = channels * (4 + 4 * chunks);
                    for (size_t n = 0; n < bytes; n++) {
                        encoded[n] = n * 53 + seed * 71 + index;
                    }
                    for (int ch = 0; ch < channels; ch++) {
                        encoded[ch * 4] = seed * 83;
                        encoded[ch * 4 + 1] = seed * 91;
                        encoded[ch * 4 + 2] = (index + ch * 43) % 89;
                        encoded[ch * 4 + 3] = 0;
                    }
                    int before = scalar_decode_block(reference, encoded,
                                                     bytes, channels);
                    int after = adpcm_decode_block(&table, candidate, encoded,
                                                    bytes, channels);
                    assert(before == after);
                    assert(memcmp(reference, candidate,
                                  before * channels * sizeof(int16_t)) == 0);
                    assert(memcmp(&table, &original, sizeof(table)) == 0);
                    cases++;
                }
            }
        }
    }
    printf("PASS: %u production decoder differential cases; PCM exact; table immutable\n", cases);
    return 0;
}
