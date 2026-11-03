/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/mcpx/apu/dsp/ep-native-input.h"
#include "hw/xbox/mcpx/apu/dsp/ep-native-policy.h"

typedef struct Fixture {
    unsigned reads, validations, fail_read, fail_validation;
    unsigned phase, slot;
    bool bad_word;
} Fixture;

static bool mock_read(void *opaque, uint32_t address, uint8_t *data, size_t len)
{
    Fixture *f = opaque;
    unsigned index = f->reads++;
    if (index == f->fail_read)
        return false;
    if (f->phase == 0) {
        if (index == 0) {
            if (address != 0x73f8 || len != 96)
                return false;
            memset(data, 0xa6, len);
            return true;
        }
        index--;
    }
    if (index == 0) {
        if (address != 0x70d4 || len != 804)
            return false;
        memset(data, 0xb7, len);
        return true;
    }
    index--;
    if (index >= 5 || address != 0x10000 + f->slot * 4 + index * 2048 ||
        len != 1024)
        return false;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t value = (index << 16) | i;
        for (unsigned j = 0; j < 4; j++)
            data[i * 4 + j] = value >> (8 * j);
    }
    if (f->bad_word && index == 4) {
        data[1023] = 1;
    }
    return true;
}

static bool mock_validate(void *opaque, const uint8_t *data, size_t len)
{
    Fixture *f = opaque;
    unsigned call = f->validations++;
    if (call == f->fail_validation)
        return false;
    return (len == 96 && data[0] == 0xa6) || (len == 804 && data[0] == 0xb7);
}

int main(void)
{
    if (!ep_native_can_execute(true, true, true, true, true, true, false))
        return 20;
    if (ep_native_can_execute(false, true, true, true, true, true, false) ||
        ep_native_can_execute(true, false, true, true, true, true, false) ||
        ep_native_can_execute(true, true, false, true, true, true, false) ||
        ep_native_can_execute(true, true, true, false, true, true, false) ||
        ep_native_can_execute(true, true, true, true, false, true, false) ||
        ep_native_can_execute(true, true, true, true, true, false, false) ||
        ep_native_can_execute(true, true, true, true, true, true, true))
        return 21;
    uint32_t input[5][256], saved[5][256];
    Fixture f = {
        .fail_read = 99, .fail_validation = 99, .phase = 0, .slot = 0x100
    };
    if (!ep_native_prepare_input(&f, mock_read, mock_validate, f.phase, f.slot,
                                 input) ||
        f.reads != 7 || f.validations != 2 || input[4][255] != 0x400ff)
        return 1;
    f = (Fixture){
        .fail_read = 99, .fail_validation = 99, .phase = 5, .slot = 0
    };
    if (!ep_native_prepare_input(&f, mock_read, mock_validate, f.phase, f.slot,
                                 input) ||
        f.reads != 6 || f.validations != 1 || input[3][42] != 0x3002a)
        return 2;
    memset(input, 0x5a, sizeof(input));
    memcpy(saved, input, sizeof(input));
    f = (Fixture){
        .fail_read = 3, .fail_validation = 99, .phase = 0, .slot = 0
    };
    if (ep_native_prepare_input(&f, mock_read, mock_validate, f.phase, f.slot,
                                input) ||
        memcmp(input, saved, sizeof(input)))
        return 3;
    f = (Fixture){
        .fail_read = 99, .fail_validation = 0, .phase = 5, .slot = 0
    };
    if (ep_native_prepare_input(&f, mock_read, mock_validate, f.phase, f.slot,
                                input) ||
        f.reads != 1 || memcmp(input, saved, sizeof(input)))
        return 4;
    f = (Fixture){
        .fail_read = 99, .fail_validation = 99, .phase = 6, .slot = 0
    };
    if (ep_native_prepare_input(&f, mock_read, mock_validate, f.phase, f.slot,
                                input) ||
        f.reads || memcmp(input, saved, sizeof(input)))
        return 5;
    f = (Fixture){ .fail_read = 99,
                   .fail_validation = 99,
                   .phase = 5,
                   .slot = 0,
                   .bad_word = true };
    if (ep_native_prepare_input(&f, mock_read, mock_validate, f.phase, f.slot,
                                input) ||
        f.reads != 6 || memcmp(input, saved, sizeof(input)))
        return 6;
    f = (Fixture){
        .fail_read = 99, .fail_validation = 99, .phase = 5, .slot = 1
    };
    if (ep_native_prepare_input(&f, mock_read, mock_validate, f.phase, f.slot,
                                input) ||
        f.reads || memcmp(input, saved, sizeof(input)))
        return 7;
    return 0;
}
