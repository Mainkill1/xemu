#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_REG(name, insn, expected)                                    \
    do {                                                                  \
        uint64_t result;                                                  \
        asm volatile("movq %1, %%mm0\n\t"                                \
                     "movq %2, %%mm1\n\t"                                \
                     insn " %%mm1, %%mm0\n\t"                            \
                     "movq %%mm0, %0\n\t"                                \
                     "emms"                                              \
                     : "=m"(result)                                      \
                     : "m"(a), "m"(b)                                   \
                     : "mm0", "mm1");                                   \
        if (result != (expected)) {                                       \
            fprintf(stderr, "%s: got %016" PRIx64 ", expected %016"     \
                    PRIx64 "\n", name, result, (uint64_t)(expected));     \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int main(void)
{
    const uint64_t a = UINT64_C(0x0706050403020100);
    const uint64_t b = UINT64_C(0x1716151413121110);
    uint64_t result;

    TEST_REG("punpcklbw", "punpcklbw", UINT64_C(0x1303120211011000));
    TEST_REG("punpckhbw", "punpckhbw", UINT64_C(0x1707160615051404));
    TEST_REG("punpcklwd", "punpcklwd", UINT64_C(0x1312030211100100));
    TEST_REG("punpckhwd", "punpckhwd", UINT64_C(0x1716070615140504));
    TEST_REG("punpckldq", "punpckldq", UINT64_C(0x1312111003020100));
    TEST_REG("punpckhdq", "punpckhdq", UINT64_C(0x1716151407060504));

    asm volatile("movq %1, %%mm0\n\t"
                 "punpcklbw %2, %%mm0\n\t"
                 "movq %%mm0, %0\n\t"
                 "emms"
                 : "=m"(result)
                 : "m"(a), "m"(b)
                 : "mm0");
    if (result != UINT64_C(0x1303120211011000)) {
        fprintf(stderr, "punpcklbw memory: got %016" PRIx64 "\n", result);
        return 1;
    }

    asm volatile("movq %1, %%mm0\n\t"
                 "punpcklbw %%mm0, %%mm0\n\t"
                 "movq %%mm0, %0\n\t"
                 "emms"
                 : "=m"(result)
                 : "m"(a)
                 : "mm0");
    if (result != UINT64_C(0x0303020201010000)) {
        fprintf(stderr, "punpcklbw alias: got %016" PRIx64 "\n", result);
        return 1;
    }

    return 0;
}
