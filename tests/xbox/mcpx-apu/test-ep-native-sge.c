/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "hw/xbox/mcpx/apu/dsp/ep-native-input.h"
#include "hw/xbox/mcpx/apu/dsp/ep-native-sge.h"

typedef struct Memory {
    uint8_t bytes[0x40000];
    bool fail;
} Memory;
static bool read_physical(void *opaque, uint64_t address, uint8_t *dst,
                          size_t len)
{
    Memory *m = opaque;
    if (m->fail || address > sizeof(m->bytes) ||
        len > sizeof(m->bytes) - address)
        return false;
    memcpy(dst, m->bytes + address, len);
    return true;
}

static bool read_scratch(void *opaque, uint32_t addr, uint8_t *dst, size_t len)
{
    return ep_native_sge_read(opaque, read_physical,
                              sizeof(((Memory *)0)->bytes), 0x100, 0x13, addr,
                              dst, len);
}

static bool validate_overlay(void *opaque, const uint8_t *data, size_t len)
{
    (void)opaque;
    return (len == 96 && data[0] == 0xa6) || (len == 804 && data[0] == 0xb7);
}
static void put32(Memory *m, unsigned pos, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        m->bytes[pos + i] = value >> (8 * i);
}
int main(void)
{
    static Memory m;
    uint8_t out[1024], unchanged[1024];
    put32(&m, 0x100, 0x2000);
    put32(&m, 0x108, 0x4000);
    for (unsigned i = 0; i < 4096; i++) {
        m.bytes[0x2000 + i] = i;
        m.bytes[0x4000 + i] = i ^ 0x55;
    }
    if (!ep_native_sge_read(&m, read_physical, sizeof(m.bytes), 0x100, 1,
                            4096 - 256, out, 512))
        return 1;
    for (unsigned i = 0; i < 256; i++)
        if (out[i] != (uint8_t)i || out[256 + i] != (uint8_t)(i ^ 0x55))
            return 2;
    memset(out, 0x5a, sizeof(out));
    memcpy(unchanged, out, sizeof(out));
    if (ep_native_sge_read(&m, read_physical, sizeof(m.bytes), 0x100, 0,
                           4096 - 256, out, 512) ||
        memcmp(out, unchanged, sizeof(out)))
        return 3;
    put32(&m, 0x108, sizeof(m.bytes) - 128);
    if (ep_native_sge_read(&m, read_physical, sizeof(m.bytes), 0x100, 1,
                           4096 - 256, out, 512) ||
        memcmp(out, unchanged, sizeof(out)))
        return 4;
    if (ep_native_sge_read(&m, read_physical, sizeof(m.bytes), UINT64_MAX - 2,
                           1, 0, out, 16) ||
        memcmp(out, unchanged, sizeof(out)))
        return 5;
    if (ep_native_sge_read(&m, read_physical, sizeof(m.bytes), 0x100, 1, 0, out,
                           1025) ||
        memcmp(out, unchanged, sizeof(out)))
        return 6;
    m.fail = true;
    if (ep_native_sge_read(&m, read_physical, sizeof(m.bytes), 0x100, 1, 0, out,
                           16) ||
        memcmp(out, unchanged, sizeof(out)))
        return 7;
    m.fail = false;
    for (unsigned i = 0; i <= 0x13; i++) {
        put32(&m, 0x100 + 8 * i, 0x10000 + i * 4096);
    }
    m.bytes[0x10000 + 0x73f8] = 0xa6;
    m.bytes[0x10000 + 0x70d4] = 0xb7;
    for (unsigned ch = 0; ch < 5; ch++) {
        for (unsigned i = 0; i < 256; i++) {
            put32(&m, 0x20000 + ch * 2048 + i * 4, (ch << 16) | i);
        }
    }
    uint32_t input[5][256];
    if (!ep_native_prepare_input(&m, read_scratch, validate_overlay, 0, 0,
                                 input) ||
        input[4][255] != 0x400ff) {
        return 8;
    }
    return 0;
}
