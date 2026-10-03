/*
 * ADPCM decoder from the ADPCM-XQ project: https://github.com/dbry/adpcm-xq
 *
 *                        Copyright (c) David Bryant
 *                           All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Conifer Software nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ADPCM_DECODE_H
#define ADPCM_DECODE_H

/********************************* 4-bit ADPCM decoder ********************************/

#define MCPX_ADPCM_INDEX_COUNT 89
#define MCPX_ADPCM_NIBBLE_COUNT 16

typedef struct MCPXAPUADPCMDecodeTable {
    int32_t delta[MCPX_ADPCM_INDEX_COUNT][MCPX_ADPCM_NIBBLE_COUNT];
    uint8_t next_index[MCPX_ADPCM_INDEX_COUNT][MCPX_ADPCM_NIBBLE_COUNT];
} MCPXAPUADPCMDecodeTable;

static inline void
mcpx_apu_adpcm_decode_table_init(MCPXAPUADPCMDecodeTable *table)
{
    static const uint16_t step_table[MCPX_ADPCM_INDEX_COUNT] = {
        7,     8,     9,     10,    11,    12,    13,    14,
        16,    17,    19,    21,    23,    25,    28,    31,
        34,    37,    41,    45,    50,    55,    60,    66,
        73,    80,    88,    97,    107,   118,   130,   143,
        157,   173,   190,   209,   230,   253,   279,   307,
        337,   371,   408,   449,   494,   544,   598,   658,
        724,   796,   876,   963,   1060,  1166,  1282,  1411,
        1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
        3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
        7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767
    };
    static const int8_t index_table[8] = {
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    for (int index = 0; index < MCPX_ADPCM_INDEX_COUNT; index++) {
        int step = step_table[index];

        for (int nibble = 0; nibble < MCPX_ADPCM_NIBBLE_COUNT; nibble++) {
            int delta = step >> 3;
            int next_index = index + index_table[nibble & 7];

            if (nibble & 1) {
                delta += step >> 2;
            }
            if (nibble & 2) {
                delta += step >> 1;
            }
            if (nibble & 4) {
                delta += step;
            }
            if (nibble & 8) {
                delta = -delta;
            }

            table->delta[index][nibble] = delta;
            table->next_index[index][nibble] =
                next_index < 0 ? 0 : next_index > 88 ? 88 : next_index;
        }
    }
}

/* Decode the block of ADPCM data into PCM. This requires no context because ADPCM blocks
 * are indeppendently decodable. This assumes that a single entire block is always decoded;
 * it must be called multiple times for multiple blocks and cannot resume in the middle of a
 * block.
 *
 * Parameters:
 *  outbuf          destination for interleaved PCM samples
 *  inbuf           source ADPCM block
 *  inbufsize       size of source ADPCM block
 *  channels        number of channels in block (must be determined from other context)
 *
 * Returns number of converted composite samples (total samples divided by number of channels)
 */

static inline int
adpcm_decode_block(const MCPXAPUADPCMDecodeTable *table, int16_t *outbuf,
                   const uint8_t *inbuf, size_t inbufsize, int channels)
{
    #define CLIP(data, min, max) \
    if ((data) > (max)) data = max; \
    else if ((data) < (min)) data = min;

    int samples = 1, chunks;
    int32_t pcmdata[2];
    int8_t index[2];

    if (inbufsize < (uint32_t) channels * 4)
        return 0;

    for (int ch = 0; ch < channels; ch++) {
        *outbuf++ = pcmdata[ch] = (int16_t) (inbuf [0] | (inbuf [1] << 8));
        index[ch] = inbuf [2];

        if (index [ch] < 0 || index [ch] > 88 || inbuf [3])     // sanitize the input a little...
            return 0;

        inbufsize -= 4;
        inbuf += 4;
    }

    chunks = inbufsize / (channels * 4);
    samples += chunks * 8;

    while (chunks--) {

        for (int ch = 0; ch < channels; ++ch) {

            for (int i = 0; i < 4; ++i) {
                uint8_t nibble = *inbuf & 0xf;

                pcmdata[ch] += table->delta[index[ch]][nibble];
                index[ch] = table->next_index[index[ch]][nibble];
                CLIP(pcmdata[ch], -32768, 32767);
                outbuf [i * 2 * channels] = pcmdata[ch];

                nibble = *inbuf >> 4;

                pcmdata[ch] += table->delta[index[ch]][nibble];
                index[ch] = table->next_index[index[ch]][nibble];
                CLIP(pcmdata[ch], -32768, 32767);
                outbuf [(i * 2 + 1) * channels] = pcmdata[ch];

                inbuf++;
            }

            outbuf++;
        }

        outbuf += channels * 7;
    }

    return samples;
}

#endif
