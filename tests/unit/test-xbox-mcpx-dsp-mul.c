/*
 * MCPX C-interpreter arithmetic regression and opt-in benchmark.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Include the real translation unit to reach its private multiply without
 * adding a production API or an extra call in DSP instruction handlers.
 */
#include "qemu/osdep.h"

#ifndef XEMU_DSP_CPU_SOURCE
#define XEMU_DSP_CPU_SOURCE "../../hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c"
#endif
#include XEMU_DSP_CPU_SOURCE

#define OPERAND_COUNT 4096
#define RESULT_MASK UINT64_C(0x00ffffffffffffff)

static const uint32_t boundaries[] = {
    0,        1,        2,        0xfff,    0x1000,   0x7ffffe,
    0x7fffff, 0x800000, 0x800001, 0xfffffe, 0xffffff,
};

static uint32_t random_word(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

/*
 * Bit-serial unsigned multiplication, independent of the native signed
 * multiply under test. The 56-bit result is a fractional two's complement.
 */
static uint64_t reference_product(uint32_t x, uint32_t y, bool negate)
{
    x &= 0xffffff;
    y &= 0xffffff;
    if (x & 0x800000) {
        x = 0x1000000 - x;
        negate = !negate;
    }
    if (y & 0x800000) {
        y = 0x1000000 - y;
        negate = !negate;
    }
    uint64_t product = 0;
    for (unsigned bit = 0; bit < 24; bit++) {
        if (y & (1u << bit)) {
            product += (uint64_t)x << bit;
        }
    }
    product *= 2;
    return (negate ? -product : product) & RESULT_MASK;
}

static uint64_t result_bits(const uint32_t words[3])
{
    return (uint64_t)words[0] << 48 | (uint64_t)words[1] << 24 | words[2];
}

static void check_product(uint32_t x, uint32_t y, uint8_t sign)
{
    uint32_t actual[3] = { 0xdeadbeef, 0xdeadbeef, 0xdeadbeef };
    dsp_mul56(x, y, actual, sign);
    g_assert_cmphex(actual[0] & ~0xffu, ==, 0);
    g_assert_cmphex(actual[1] & ~0xffffffu, ==, 0);
    g_assert_cmphex(actual[2] & ~0xffffffu, ==, 0);
    g_assert_cmphex(result_bits(actual), ==, reference_product(x, y, sign));
}

static void test_boundaries(void)
{
    for (unsigned i = 0; i < ARRAY_SIZE(boundaries); i++) {
        for (unsigned j = 0; j < ARRAY_SIZE(boundaries); j++) {
            check_product(boundaries[i], boundaries[j], SIGN_PLUS);
            check_product(boundaries[i], boundaries[j], SIGN_MINUS);
        }
    }
}

static void test_random_products(void)
{
    uint32_t seed = 0x52aac3ec;
    for (unsigned i = 0; i < 1000000; i++) {
        uint32_t x = random_word(&seed) & 0xffffff;
        uint32_t y = random_word(&seed) & 0xffffff;
        check_product(x, y, SIGN_PLUS);
        check_product(x, y, SIGN_MINUS);
    }
}

static void test_operand_high_bits(void)
{
    for (unsigned i = 0; i < ARRAY_SIZE(boundaries); i++) {
        check_product(boundaries[i] | 0xa5000000, 0xff123456, SIGN_PLUS);
        check_product(0xff123456, boundaries[i] | 0x5a000000, SIGN_MINUS);
    }
}

static void checksum_word(GChecksum *sum, uint32_t word)
{
    uint8_t little_endian[4];
    stl_le_p(little_endian, word);
    g_checksum_update(sum, little_endian, sizeof(little_endian));
}

/*
 * Replay every parallel MPY/MPYR/MAC/MACR register variant through real
 * fetch/dispatch/rounding/flags/PC handling, including each scaling mode.
 * The expected digest is pinned to the unchanged parent interpreter.
 */
static char *instruction_digest(void)
{
    dsp_core_t *core = g_new0(dsp_core_t, 1);
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    for (unsigned scaling = 0; scaling < 3; scaling++) {
        for (unsigned opcode = 0x80; opcode <= 0xff; opcode++) {
            for (unsigned i = 0; i < ARRAY_SIZE(boundaries); i++) {
                for (unsigned j = 0; j < ARRAY_SIZE(boundaries); j++) {
                    dsp56k_reset_cpu(core);
                    core->registers[DSP_REG_SR] |= scaling << DSP_SR_S0;
                    core->registers[DSP_REG_X0] = boundaries[i];
                    core->registers[DSP_REG_X1] = boundaries[j];
                    core->registers[DSP_REG_Y0] = boundaries[j];
                    core->registers[DSP_REG_Y1] = boundaries[i];
                    core->registers[DSP_REG_A0] = 0xffffff;
                    core->registers[DSP_REG_A1] = 0x7fffff;
                    core->registers[DSP_REG_A2] = 0;
                    core->registers[DSP_REG_B0] = 1;
                    core->registers[DSP_REG_B1] = 0x800000;
                    core->registers[DSP_REG_B2] = 0xff;
                    dsp56k_write_memory(core, DSP_SPACE_P, 0,
                                        0x200000 | opcode);
                    dsp56k_execute_instruction(core);
                    g_assert_cmpuint(core->pc, ==, 1);
                    g_assert_cmpuint(core->instr_cycle, ==, 2);
                    for (unsigned reg = 0; reg < DSP_REG_MAX; reg++) {
                        checksum_word(sum, core->registers[reg]);
                    }
                    checksum_word(sum, core->pc);
                    checksum_word(sum, core->instr_cycle);
                    checksum_word(sum, core->is_idle);
                }
            }
        }
    }
    char *result = g_strdup(g_checksum_get_string(sum));
    g_checksum_free(sum);
    g_free(core);
    return result;
}

static void test_instruction_state(void)
{
    g_autofree char *digest = instruction_digest();
    g_assert_cmpstr(
        digest, ==,
        "7591ff4ee4a7f31b8971da505208bda80e84b492c451fb27164c860f39e07772");
}

typedef struct Operand {
    uint32_t x, y;
    uint8_t sign;
} Operand;

static __attribute__((noinline)) uint64_t
benchmark_helper(const Operand *operands, uint64_t iterations)
{
    __asm__ __volatile__("" : : : "memory");
    uint64_t checksum = 0;
    for (uint64_t i = 0; i < iterations; i++) {
        const Operand *operand = &operands[i & (OPERAND_COUNT - 1)];
        uint32_t result[3];
        dsp_mul56(operand->x, operand->y, result, operand->sign);
        checksum += result[0] + result[1] + result[2];
    }
    __asm__ __volatile__("" : : : "memory");
    return checksum;
}

static __attribute__((noinline)) uint64_t
benchmark_helper_call(const Operand *operands, uint64_t iterations)
{
    /* Match call boundaries even when the smaller candidate would inline. */
    void (*volatile multiply)(uint32_t, uint32_t, uint32_t *, uint8_t) =
        dsp_mul56;
    __asm__ __volatile__("" : : : "memory");
    uint64_t checksum = 0;
    for (uint64_t i = 0; i < iterations; i++) {
        const Operand *operand = &operands[i & (OPERAND_COUNT - 1)];
        uint32_t result[3];
        multiply(operand->x, operand->y, result, operand->sign);
        checksum += result[0] + result[1] + result[2];
    }
    __asm__ __volatile__("" : : : "memory");
    return checksum;
}

static __attribute__((noinline)) uint64_t
benchmark_instructions(dsp_core_t *core, uint64_t iterations)
{
    __asm__ __volatile__("" : : : "memory");
    uint64_t checksum = 0;
    for (uint64_t i = 0; i < iterations; i++) {
        core->pc &= DSP_PRAM_SIZE - 1;
        dsp56k_execute_instruction(core);
        checksum += core->registers[DSP_REG_A0] + core->registers[DSP_REG_A1] +
                    core->registers[DSP_REG_A2] + core->registers[DSP_REG_B0] +
                    core->registers[DSP_REG_B1] + core->registers[DSP_REG_B2] +
                    core->registers[DSP_REG_SR];
    }
    __asm__ __volatile__("" : : : "memory");
    return checksum;
}

static int benchmark(const char *mode, uint64_t iterations)
{
    Operand operands[OPERAND_COUNT];
    uint32_t seed = 0x52aac3ec;
    for (unsigned i = 0; i < OPERAND_COUNT; i++) {
        operands[i].x = random_word(&seed) & 0xffffff;
        operands[i].y = random_word(&seed) & 0xffffff;
        operands[i].sign = random_word(&seed) & 1;
    }
    dsp_core_t *core = g_new0(dsp_core_t, 1);
    dsp56k_reset_cpu(core);
    core->registers[DSP_REG_X0] = 0x6abcde;
    core->registers[DSP_REG_X1] = 0x9abcde;
    core->registers[DSP_REG_Y0] = 0x456789;
    core->registers[DSP_REG_Y1] = 0xfedcba;
    for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
        uint32_t opcode = !strcmp(mode, "mac") ? 0xd2 : 0x80 + (i & 0x7f);
        dsp56k_write_memory(core, DSP_SPACE_P, i, 0x200000 | opcode);
    }
    bool helper = !strcmp(mode, "helper") || !strcmp(mode, "helper-call");
    uint64_t (*helper_batch)(const Operand *, uint64_t) =
        !strcmp(mode, "helper-call") ? benchmark_helper_call : benchmark_helper;
    if (!helper && strcmp(mode, "mac") && strcmp(mode, "mixed")) {
        fprintf(stderr,
                "Benchmark mode must be helper, helper-call, mac or mixed.\n");
        g_free(core);
        return 2;
    }
    /*
     * Warm up outside timing. Instruction state resumes identically in both
     * binaries; all inputs and the checksum contract are fixed.
     */
    uint64_t warmup_checksum = helper ? helper_batch(operands, 65536) :
                                        benchmark_instructions(core, 65536);
    int64_t start = g_get_monotonic_time();
    uint64_t checksum = helper ? helper_batch(operands, iterations) :
                                 benchmark_instructions(core, iterations);
    int64_t elapsed_us = g_get_monotonic_time() - start;
    if (elapsed_us <= 0) {
        fprintf(
            stderr,
            "Elapsed time is below clock resolution; increase ITERATIONS.\n");
        g_free(core);
        return 2;
    }
    printf("{\"mode\":\"%s\",\"iterations\":%" PRIu64 ",\"elapsedUs\":%" PRId64
           ",\"nsPerOperation\":%.6f"
           ",\"checksum\":\"%016" PRIx64 "\",\"warmupChecksum\":\"%016" PRIx64
           "\",\"seed\":\"52aac3ec\"}\n",
           mode, iterations, elapsed_us, elapsed_us * 1000.0 / iterations,
           checksum, warmup_checksum);
    g_free(core);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--fingerprint")) {
        g_autofree char *digest = instruction_digest();
        puts(digest);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--benchmark")) {
        if (argc != 4) {
            fprintf(stderr,
                    "Usage: %s --benchmark helper|helper-call|mac|mixed "
                    "ITERATIONS\n",
                    argv[0]);
            return 2;
        }
        char *end;
        errno = 0;
        uint64_t iterations = g_ascii_strtoull(argv[3], &end, 10);
        if (errno || !argv[3][0] || argv[3][0] == '-' || *end ||
            iterations < 1 || iterations > 1000000000) {
            fprintf(stderr, "ITERATIONS must be 1..1000000000.\n");
            return 2;
        }
        return benchmark(argv[2], iterations);
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/dsp/mul/boundaries", test_boundaries);
    g_test_add_func("/xbox/dsp/mul/random", test_random_products);
    g_test_add_func("/xbox/dsp/mul/operand-high-bits", test_operand_high_bits);
    g_test_add_func("/xbox/dsp/mul/instruction-state", test_instruction_state);
    return g_test_run();
}
