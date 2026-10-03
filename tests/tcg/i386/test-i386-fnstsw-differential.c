/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compare FNSTSW AX with the helper-backed FNSTSW memory form. */

#include <stdint.h>
#include <stdio.h>

typedef struct __attribute__((packed)) FpuEnvironment {
    uint16_t control;
    uint16_t reserved_control;
    uint16_t status;
    uint16_t reserved_status;
    uint16_t tags;
    uint16_t reserved_tags;
    uint32_t ignored[4];
} FpuEnvironment;

static uint16_t read_ax_status(void)
{
    uint16_t status;

    __asm__ volatile("fnstsw %%ax" : "=a"(status));
    return status;
}

static uint16_t read_memory_status(void)
{
    uint16_t status;

    __asm__ volatile("fnstsw %0" : "=m"(status));
    return status;
}

int main(void)
{
    static const uint16_t condition_bits[] = {
        0x0000, 0x0100, 0x0200, 0x0300,
        0x0400, 0x0500, 0x0600, 0x0700,
        0x4000, 0x4100, 0x4200, 0x4300,
        0x4400, 0x4500, 0x4600, 0x4700,
    };
    static const uint16_t summary_bits[] = {
        0x0000, 0x0080, 0x8000, 0x8080,
    };
    static const uint16_t tag_words[] = {
        0x0000, 0x5555, 0xaaaa, 0xffff,
    };
    static const uint16_t exception_masks[] = {
        0x0000, 0x003f, 0x0001, 0x0002,
        0x0004, 0x0008, 0x0010, 0x0020,
    };
    FpuEnvironment original;

    __asm__ volatile("fnstenv %0" : "=m"(original));
    for (unsigned int mask = 0;
         mask < sizeof(exception_masks) / sizeof(exception_masks[0]); mask++) {
        for (unsigned int top = 0; top < 8; top++) {
            for (unsigned int condition = 0;
                 condition < sizeof(condition_bits) / sizeof(condition_bits[0]);
                 condition++) {
                for (unsigned int flags = 0; flags < 64; flags++) {
                    for (unsigned int summary = 0;
                         summary < sizeof(summary_bits) / sizeof(summary_bits[0]);
                         summary++) {
                        for (unsigned int tags = 0;
                             tags < sizeof(tag_words) / sizeof(tag_words[0]);
                             tags++) {
                            FpuEnvironment test = original;
                            test.control = (original.control & ~0x003f) |
                                           exception_masks[mask];
                            test.status = (uint16_t)(condition_bits[condition] |
                                                    flags |
                                                    summary_bits[summary] |
                                                    (top << 11));
                            test.tags = tag_words[tags];
                            __asm__ volatile("fnclex");
                            __asm__ volatile("fldenv %0" : : "m"(test));

                            uint16_t ax = read_ax_status();
                            uint16_t memory = read_memory_status();
                            if (ax != memory) {
                                printf("FAIL mask=%04x top=%u cc=%04x flags=%02x "
                                       "summary=%04x tags=%04x ax=%04x mem=%04x\n",
                                       exception_masks[mask], top,
                                       condition_bits[condition], flags,
                                       summary_bits[summary], tag_words[tags],
                                       ax, memory);
                                __asm__ volatile("fnclex");
                                __asm__ volatile("fldenv %0" : : "m"(original));
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    __asm__ volatile("fnclex");
    __asm__ volatile("fldenv %0" : : "m"(original));
    return 0;
}
