/*
 * Retained DSP interpreter dispatch and write-coherency tests.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Instruction encodings come from this interpreter's opcode templates.
 * Include production translation units to test private dispatch/lifecycle
 * boundaries without introducing a hot-path API in the emulator.
 */
#include "qemu/osdep.h"

#ifndef XEMU_DSP_CPU_SOURCE
#define XEMU_DSP_CPU_SOURCE "../../hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c"
#endif
#include XEMU_DSP_CPU_SOURCE

#ifndef XEMU_DSP_C_SOURCE
#define XEMU_DSP_C_SOURCE "../../hw/xbox/mcpx/apu/dsp/dsp_c.c"
#endif
#include XEMU_DSP_C_SOURCE

/* These tests never access external APU peripherals or start an audio frame.
 * Fail on an unexpected dependency call; all cache/lifecycle code is real. */
uint32_t read_peripheral(DSPState *dsp, uint32_t address)
{
    g_assert_not_reached();
}

void write_peripheral(DSPState *dsp, uint32_t address, uint32_t value)
{
    g_assert_not_reached();
}

void dsp_start_frame_impl(DSPState *dsp)
{
    g_assert_not_reached();
}

static uint32_t opcode(const char *name, uint32_t fields)
{
    for (unsigned i = 0; i < ARRAY_SIZE(nonparallel_opcodes); i++) {
        if (!strcmp(nonparallel_opcodes[i].name, name)) {
            return nonparallel_matches[i][1] |
                   (fields & ~nonparallel_matches[i][0]);
        }
    }
    g_error("Unknown production opcode: %s", name);
    return 0;
}

static dsp_core_t *new_core(void)
{
    dsp_core_t *core = g_new0(dsp_core_t, 1);
    dsp56k_reset_cpu(core);
    return core;
}

static void put(dsp_core_t *core, unsigned address, uint32_t word)
{
    dsp56k_write_memory(core, DSP_SPACE_P, address, word);
}

static void step_at(dsp_core_t *core, unsigned address)
{
    core->pc = address;
    dsp56k_execute_instruction(core);
}

/* Registers and output, rather than cache representation, are the oracle. */
static void test_warmed_overwrite(void)
{
    dsp_core_t *core = new_core();
    uint32_t move = opcode("movec #xx, D1", 0x2300 | DSP_REG_LA);
    put(core, 7, move);
    step_at(core, 7);
    g_assert_cmpuint(core->registers[DSP_REG_LA], ==, 0x23);
    put(core, 7, opcode("jmp xxx", 123));
    step_at(core, 7);
    g_assert_cmpuint(core->pc, ==, 123);
    put(core, 7, 0x200013); /* Parallel no-move CLR A, production ALU table. */
    core->registers[DSP_REG_A1] = 0x654321;
    step_at(core, 7);
    g_assert_cmpuint(core->registers[DSP_REG_A1], ==, 0);
    put(core, 7, move);
    core->registers[DSP_REG_LA] = 0;
    step_at(core, 7);
    g_assert_cmpuint(core->registers[DSP_REG_LA], ==, 0x23);
    g_free(core);
}

static void test_parallel_cache(void)
{
    dsp_core_t *core = new_core();
    put(core, DSP_PRAM_SIZE - 1, 0x200013);
    step_at(core, DSP_PRAM_SIZE - 1);
    /* Cache all instruction classes, including the supported last P slot. */
    g_assert_nonnull(core->pram_opcache[DSP_PRAM_SIZE - 1]);
    g_assert_cmpuint(core->pc, ==, DSP_PRAM_SIZE);
    g_free(core);
}

static void test_reset_discards_derived_cache(void)
{
    dsp_core_t *core = new_core();
    put(core, 0, opcode("nop", 0));
    step_at(core, 0);
    g_assert_nonnull(core->pram_opcache[0]);
    dsp56k_reset_cpu(core);
    g_assert_null(core->pram_opcache[0]);
    step_at(core, 0);
    g_assert_cmpuint(core->pc, ==, 1);
    g_free(core);
}

static void test_extension_and_loops(void)
{
    dsp_core_t *core = new_core();
    put(core, 0, opcode("do #xxx, expr", 0x0300));
    put(core, 1, 4);
    step_at(core, 0);
    g_assert_cmpuint(core->registers[DSP_REG_LA], ==, 4);
    g_assert_cmpuint(core->cur_inst_len, ==, 2);
    g_assert_cmpuint(core->instr_cycle, ==, 6);
    /* Change only the dynamically fetched extension of a warmed handler. */
    put(core, 1, 6);
    step_at(core, 0);
    g_assert_cmpuint(core->registers[DSP_REG_LA], ==, 6);
    dsp56k_reset_cpu(core);
    put(core, 0, opcode("rep #xxx", 0x0300));
    put(core, 1, opcode("movec #xx, D1", 0x4700 | DSP_REG_LA));
    step_at(core, 0);
    for (unsigned i = 0; i < 3; i++) {
        dsp56k_execute_instruction(core);
        g_assert_cmpuint(core->pc, ==, i == 2 ? 2 : 1);
        g_assert_cmpuint(core->registers[DSP_REG_LA], ==, 0x47);
    }
    g_assert_cmpuint(core->loop_rep, ==, 0);
    g_free(core);
}

static void test_self_write(void)
{
    dsp_core_t *core = new_core();
    put(core, 5, opcode("nop", 0));
    step_at(core, 5);
    /* Production MOVEM P:aa <- R, W=0, aa=5, D=X0. */
    core->registers[DSP_REG_X0] = opcode("jmp xxx", 42);
    uint32_t movem = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(nonparallel_opcodes); i++) {
        if (!strcmp(nonparallel_opcodes[i].template,
                    "00000111W0aaaaaa00dddddd")) {
            movem = nonparallel_matches[i][1] | (5 << 8) | DSP_REG_X0;
            break;
        }
    }
    g_assert_cmpuint(movem, !=, 0);
    put(core, 0, movem);
    step_at(core, 0);
    step_at(core, 5);
    g_assert_cmpuint(core->pc, ==, 42);
    g_free(core);
}

static void bootstrap_image(void *opaque, uint8_t *ptr, uint32_t address,
                            size_t length, bool write)
{
    g_assert_false(write);
    g_assert_cmpuint(address, ==, 0);
    g_assert_cmpuint(length, ==, 0x800 * 4);
    memset(ptr, 0, length);
    stl_le_p(ptr, *(uint32_t *)opaque);
}

static void test_backend_lifecycle(void)
{
    DSPState *dsp = g_new0(DSPState, 1);
    dsp_c_init(dsp);
    dsp_core_t *core = c_core(dsp);
    put(core, 0, opcode("nop", 0));
    step_at(core, 0);
    c_dma_mem_write(core, DSP_SPACE_P, 0, opcode("jmp xxx", 23));
    step_at(core, 0);
    g_assert_cmpuint(core->pc, ==, 23);

    uint32_t image = opcode("movec #xx, D1", 0x6800 | DSP_REG_LA);
    dsp->dma.scratch_rw = bootstrap_image;
    dsp->dma.rw_opaque = &image;
    dsp_c_bootstrap(dsp);
    g_assert_null(core->pram_opcache[0]);
    step_at(core, 0);
    g_assert_cmpuint(core->registers[DSP_REG_LA], ==, 0x68);

    dsp_c_sync_to_vm(dsp);
    stl_le_p(&dsp->core.pram[0], opcode("jmp xxx", 73));
    dsp_c_sync_from_vm(dsp);
    g_assert_null(core->pram_opcache[0]);
    step_at(core, 0);
    g_assert_cmpuint(core->pc, ==, 73);
    dsp_c_invalidate_opcache(dsp);
    g_assert_null(core->pram_opcache[0]);
    step_at(core, 0);
    dsp_c_reset(dsp);
    g_assert_null(core->pram_opcache[0]);
    g_assert_cmpuint(dsp->save_cycles, ==, 0);

    /* Recreate the C backend from the shared VM state, as a backend switch
     * does. No host handler pointers exist in that serializable structure. */
    g_free(core);
    dsp_c_init(dsp);
    dsp_c_sync_from_vm(dsp);
    step_at(c_core(dsp), 0);
    g_assert_cmpuint(c_core(dsp)->pc, ==, 73);
    g_free(dsp->backend);
    g_free(dsp);
}

static void test_wait_interrupt(void)
{
    dsp_core_t *core = new_core();
    put(core, 0, opcode("wait", 0));
    for (unsigned i = 1; i < 12; i++) {
        put(core, i, opcode("nop", 0));
    }
    /* Warm both fast-interrupt vector instructions before injecting IRQ. */
    step_at(core, 8);
    step_at(core, 9);
    core->pc = 0;
    dsp56k_add_interrupt(core, DSP_INTER_TRAP);
    const unsigned expected_pc[] = { 1, 2, 8, 9, 3, 4, 5 };
    for (unsigned i = 0; i < ARRAY_SIZE(expected_pc); i++) {
        dsp56k_execute_instruction(core);
        g_assert_cmpuint(core->pc, ==, expected_pc[i]);
        g_assert_cmpuint(core->instr_cycle, ==, 2);
    }
    g_assert_cmpuint(core->interrupt_state, ==, DSP_INTERRUPT_NONE);
    g_assert_cmpuint(core->interrupt_counter, ==, 0);
    g_free(core);
}

static void test_undefined(void)
{
    dsp_core_t *core = new_core();
    core->exception_debugging = false;
    put(core, 0, opcode("do_f", 0));
    for (unsigned i = 0; i < 2; i++) {
        step_at(core, 0);
        g_assert_cmpuint(core->pc, ==, 0);
        g_assert_cmpuint(core->cur_inst_len, ==, 0);
        g_assert_cmpuint(core->instr_cycle, ==, 102);
    }
    g_free(core);
}

static void test_traced_dispatch(void)
{
    dsp_core_t *core = new_core();
    uint32_t instruction = opcode("movec #xx, D1", 0x2300 | DSP_REG_LA);
    put(core, 0, instruction);
    trace_event_set_state_dynamic(
        &_TRACE_DSP56K_EXECUTE_INSTRUCTION_DISASM_EVENT, true);
    for (unsigned i = 0; i < 2; i++) {
        step_at(core, 0);
        g_assert_cmpuint(core->disasm_cur_inst, ==, instruction);
        g_assert_cmpuint(core->disasm_prev_inst_pc, ==, 0);
        g_assert_cmpuint(core->registers[DSP_REG_LA], ==, 0x23);
    }
    trace_event_set_state_dynamic(
        &_TRACE_DSP56K_EXECUTE_INSTRUCTION_DISASM_EVENT, false);
    g_free(core);
}

static void checksum_word(GChecksum *sum, uint32_t value)
{
    uint8_t bytes[4];
    stl_le_p(bytes, value);
    g_checksum_update(sum, bytes, sizeof(bytes));
}

static char *state_digest(dsp_core_t *core)
{
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    checksum_word(sum, core->pc);
    checksum_word(sum, core->cur_inst_len);
    checksum_word(sum, core->instr_cycle);
    checksum_word(sum, core->num_inst);
    for (unsigned i = 0; i < DSP_REG_MAX; i++) {
        checksum_word(sum, core->registers[i]);
    }
    for (unsigned i = 0; i < DSP_XRAM_SIZE; i++) {
        checksum_word(sum, core->xram[i]);
    }
    for (unsigned i = 0; i < DSP_YRAM_SIZE; i++) {
        checksum_word(sum, core->yram[i]);
    }
    char *digest = g_strdup(g_checksum_get_string(sum));
    g_checksum_free(sum);
    return digest;
}

/* Same program/state, once after cold decode and once after warming every
 * slot. This checks dispatch equivalence; independent overwrite assertions
 * above catch executing the wrong but internally consistent handler. */
static void test_cold_warm_state(void)
{
    dsp_core_t *cold = new_core();
    dsp_core_t *warm = new_core();
    cold->exception_debugging = false;
    warm->exception_debugging = false;
    uint32_t words[] = {
        opcode("movec #xx, D1", 0x2300 | DSP_REG_LA),
        0x200013,
        0x20001b,
        0x200080,
        0x2000c0,
        opcode("nop", 0),
        opcode("wait", 0),
        opcode("do_f", 0), /* Existing unimplemented diagnostic path. */
    };
    for (unsigned i = 0; i < ARRAY_SIZE(words); i++) {
        put(cold, i, words[i]);
        put(warm, i, words[i]);
        step_at(warm, i);
    }
    /* Keep derived caches, restore identical architectural inputs. */
    memset(warm->registers, 0, sizeof(warm->registers));
    memcpy(warm->registers, cold->registers, sizeof(warm->registers));
    warm->num_inst = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(words); i++) {
        step_at(cold, i);
        step_at(warm, i);
        g_autofree char *a = state_digest(cold);
        g_autofree char *b = state_digest(warm);
        g_assert_cmpstr(a, ==, b);
    }
    g_free(cold);
    g_free(warm);
}

static int benchmark(const char *mode, uint64_t iterations)
{
    dsp_core_t *core = new_core();
    unsigned size = !strcmp(mode, "working-set") || !strcmp(mode, "cold") ?
                        DSP_PRAM_SIZE :
                        32;
    bool updates = !strcmp(mode, "updates");
    bool cold = !strcmp(mode, "cold");
    if (strcmp(mode, "normal") && strcmp(mode, "parallel") &&
        strcmp(mode, "mixed") && strcmp(mode, "working-set") && !updates &&
        !cold) {
        g_free(core);
        return 2;
    }
    uint32_t normal = opcode("movec #xx, D1", 0x2300 | DSP_REG_LA);
    for (unsigned i = 0; i < size; i++) {
        uint32_t word = !strcmp(mode, "normal")   ? normal :
                        !strcmp(mode, "parallel") ? 0x200013 :
                                                    (i & 1 ? normal : 0x200013);
        put(core, i, word);
        step_at(core, i);
    }
    core->num_inst = 0;
    int64_t begin = g_get_monotonic_time();
    for (uint64_t i = 0; i < iterations; i++) {
        unsigned address = i & (size - 1);
        if (cold && address == 0) {
            memset(core->pram_opcache, 0, sizeof(core->pram_opcache));
        }
        if (updates) {
            put(core, address, i & 1 ? normal : 0x200013);
        }
        step_at(core, address);
    }
    int64_t elapsed = g_get_monotonic_time() - begin;
    g_autofree char *digest = state_digest(core);
    printf("{\"mode\":\"%s\",\"iterations\":%" PRIu64 ",\"elapsedUs\":%" PRId64
           ",\"nsPerInstruction\":%.6f,"
           "\"stateDigest\":\"%s\",\"cacheBytes\":%zu,\"slotBytes\":%zu}\n",
           mode, iterations, elapsed, elapsed * 1000.0 / iterations, digest,
           sizeof(core->pram_opcache), sizeof(core->pram_opcache[0]));
    g_free(core);
    return elapsed > 0 ? 0 : 2;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--benchmark")) {
        char *end;
        errno = 0;
        uint64_t count = g_ascii_strtoull(argv[3], &end, 10);
        if (errno || !count || count > 1000000000 || *end ||
            argv[3][0] == '-') {
            return 2;
        }
        return benchmark(argv[2], count);
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dsp/dispatch/overwrite", test_warmed_overwrite);
    g_test_add_func("/dsp/dispatch/parallel-cache", test_parallel_cache);
    g_test_add_func("/dsp/dispatch/reset", test_reset_discards_derived_cache);
    g_test_add_func("/dsp/dispatch/extension-loops", test_extension_and_loops);
    g_test_add_func("/dsp/dispatch/self-write", test_self_write);
    g_test_add_func("/dsp/dispatch/backend-lifecycle", test_backend_lifecycle);
    g_test_add_func("/dsp/dispatch/wait-interrupt", test_wait_interrupt);
    g_test_add_func("/dsp/dispatch/undefined", test_undefined);
    g_test_add_func("/dsp/dispatch/traced", test_traced_dispatch);
    g_test_add_func("/dsp/dispatch/cold-warm-state", test_cold_warm_state);
    return g_test_run();
}
