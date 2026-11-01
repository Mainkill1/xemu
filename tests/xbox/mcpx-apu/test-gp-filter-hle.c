/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/dsp.h"
#include "hw/xbox/mcpx/apu/dsp/dsp-replay.h"
#include "hw/xbox/mcpx/apu/dsp/gp-filter-hle.h"
#include "qemu/module.h"
#include "ui/xemu-settings.h"

typedef struct Fixture {
    Dsp56300State cpu;
    uint32_t x[DSP_XRAM_SIZE];
    uint32_t y[DSP_YRAM_SIZE];
} Fixture;

static unsigned ffi_runs;
void __real_dsp56300_run(Dsp56300Jit *dsp, int32_t cycles);
void __wrap_dsp56300_run(Dsp56300Jit *dsp, int32_t cycles);
void __wrap_dsp56300_run(Dsp56300Jit *dsp, int32_t cycles)
{
    ffi_runs++;
    __real_dsp56300_run(dsp, cycles);
}

static Fixture *new_fixture(uint32_t input)
{
    Fixture *f = g_new0(Fixture, 1);
    f->cpu.pc = 0x185;
    f->cpu.pc_advance = 1;
    f->cpu.registers[DSP_REG_SR] = 0xc083d4;
    f->cpu.registers[DSP_REG_OMR] = 0x300;
    f->cpu.registers[DSP_REG_LA] = 0x1c3;
    f->cpu.registers[DSP_REG_SSH] = 0x185;
    f->cpu.registers[DSP_REG_LC] = 32;
    /* dsp56300 0.1.3 core::reg::TEMP, reserved provider REP bookkeeping. */
    f->cpu.registers[0x2b] = 33;
    f->cpu.registers[DSP_REG_R0] = 0x92;
    f->cpu.registers[DSP_REG_R2] = 0x1560;
    f->cpu.registers[DSP_REG_R3] = 0xd60;
    f->cpu.registers[DSP_REG_R5] = 0x80;
    static const uint32_t offsets[8] = { 16, 16, 16, 2, 2, 0, 0, 0 };
    for (unsigned i = 0; i < 8; i++) {
        f->cpu.registers[DSP_REG_N0 + i] = offsets[i];
        f->cpu.registers[DSP_REG_M0 + i] = 0xffffff;
    }
    f->cpu.registers[DSP_REG_B1] = 0x12345;
    f->cpu.registers[DSP_REG_SP] = 4;
    for (unsigned i = 0; i < 16; i++) {
        f->cpu.stack[0][i] = 0x1100 + i;
        f->cpu.stack[1][i] = 0x2200 + i;
    }
    f->x[0x80] = 0xc000;
    f->x[0x82] = 0;
    f->x[0x83] = 8;
    f->x[0x84] = 1;
    f->x[0x85] = 0x1560;
    f->x[0x86] = 0xd60;
    /* Own identity sections and half-scale gain; no game coefficients. */
    f->x[0x87] = 0x400000;
    f->x[0x88] = 0x400000;
    f->x[0x8d] = 0x400000;
    for (unsigned i = 0; i < 32; i++) {
        f->x[0xd60 + i] = input;
    }
    return f;
}

static void test_identity(void)
{
    Fixture *f = new_fixture(3);
    g_assert_true(gp_filter_hle_run_verified(&f->cpu, f->x, f->y, 1000));
    /* Ten bodies, nine tails; the last result awaits its original store. */
    g_assert_cmpuint(f->cpu.cycle_count, ==, 1066);
    g_assert_cmpint(f->cpu.cycle_budget, ==, -66);
    g_assert_cmpuint(f->cpu.pc, ==, 0x1c0);
    g_assert_cmpuint(f->cpu.pc_advance, ==, 0);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_LC], ==, 23);
    g_assert_cmpuint(f->cpu.registers[0x2b], ==, 23);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_R2], ==, 0x156a);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_R3], ==, 0xd69);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_A1], ==, 12);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_A0], ==, 0);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_X0], ==, 24);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_Y0], ==, 24);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_Y1], ==, 24);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_B1], ==, 0x12345);
    g_assert_cmpuint(f->x[0x92], ==, 3);
    for (unsigned i = 0; i < 9; i++) {
        g_assert_cmpuint(f->x[0xd60 + i], ==, 12);
    }
    g_assert_cmpuint(f->x[0xd69], ==, 24);
    g_assert_cmpuint(f->x[0xd6a], ==, 3);
    const uint32_t history[8] = { 3, 3, 3, 3, 24, 24, 24, 24 };
    g_assert_cmpmem(f->y, sizeof(history), history, sizeof(history));
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(f->cpu.stack[0][i], ==, 0x1100 + i);
        g_assert_cmpuint(f->cpu.stack[1][i], ==, 0x2200 + i);
    }
    /* Resume a pending store with the next unchanged 1000-cycle call. */
    g_assert_true(gp_filter_hle_run_verified(&f->cpu, f->x, f->y, 1000));
    g_assert_cmpuint(f->cpu.cycle_count, ==, 2029);
    g_assert_cmpint(f->cpu.cycle_budget, ==, -29);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_LC], ==, 14);
    g_assert_cmpuint(f->cpu.pc, ==, 0x1c0);
    g_free(f);
}

static void test_limit(gconstpointer opaque)
{
    bool negative = GPOINTER_TO_UINT(opaque);
    Fixture *f = new_fixture(negative ? 0x800000 : 0x7fffff);
    g_assert_true(gp_filter_hle_run_verified(&f->cpu, f->x, f->y, 1000));
    /* The 16x pre-gain limiter runs before the half-scale multiply. */
    g_assert_cmpuint(f->x[0xd60], ==, negative ? 0xe00000 : 0x1fffff);
    g_assert_cmpuint(f->y[4], ==, negative ? 0xc00000 : 0x3fffff);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_SR] & 0xf, ==, negative ? 8 : 0);
    g_free(f);
}

static void test_budget(void)
{
    Fixture *f = new_fixture(3);
    f->cpu.cycle_budget = -37;
    g_assert_true(gp_filter_hle_run_verified(&f->cpu, f->x, f->y, 1000));
    g_assert_cmpuint(f->cpu.cycle_count, ==, 963);
    g_assert_cmpint(f->cpu.cycle_budget, ==, 0);
    g_assert_cmpuint(f->cpu.pc, ==, 0x185);
    g_assert_cmpuint(f->cpu.pc_advance, ==, 1);
    g_assert_cmpuint(f->cpu.registers[DSP_REG_LC], ==, 23);
    g_assert_cmpuint(f->x[0xd68], ==, 12);
    g_assert_cmpuint(f->x[0xd69], ==, 3);
    g_free(f);
}

static void test_normalization(gconstpointer opaque)
{
    bool negative = GPOINTER_TO_UINT(opaque);
    Fixture *f = new_fixture(negative ? 0xfa0000 : 0x60000);
    f->x[0x87] = 0x7fffff;
    for (unsigned call = 0; call < 2; call++) {
        g_assert_true(gp_filter_hle_run_verified(&f->cpu, f->x, f->y, 1000));
        g_assert_cmpuint(f->x[0xd60], ==, negative ? 0xd00000 : 0x2fffff);
        /* History is normalized even though the final output is half-scale. */
        g_assert_cmpuint(f->cpu.registers[DSP_REG_SR] & (1 << DSP_SR_U), ==, 0);
    }
    g_free(f);
}

static void test_reject(gconstpointer opaque)
{
    Fixture *f = new_fixture(3);
    switch (GPOINTER_TO_UINT(opaque)) {
    case 0:
        f->cpu.pc = 0x184;
        break;
    case 1:
        f->cpu.registers[DSP_REG_LC] = 1;
        break;
    case 2:
        f->cpu.registers[DSP_REG_LC] = 5;
        f->cpu.registers[DSP_REG_R2] = 0x157b;
        f->cpu.registers[DSP_REG_R3] = 0xd7b;
        break;
    case 3:
        f->cpu.registers[DSP_REG_OMR] = 0;
        break;
    case 4:
        f->cpu.registers[DSP_REG_M0] = 7;
        break;
    case 5:
        f->cpu.interrupts.pending_bits[1] = UINT64_C(1) << 63;
        break;
    case 6:
        f->cpu.power_state = 1;
        break;
    case 7:
        f->cpu.loop_rep = true;
        break;
    case 8:
        f->cpu.halt_requested = true;
        break;
    case 9:
        f->x[0x83] = 7;
        break;
    case 10:
        f->x[0x84] = 2;
        break;
    case 11:
        f->cpu.registers[DSP_REG_R2]++;
        break;
    case 12:
        memcpy(f->x + 0xd60, f->x + 0x80, 19 * sizeof(uint32_t));
        f->cpu.registers[DSP_REG_R5] = 0xd60;
        f->cpu.registers[DSP_REG_R0] = 0xd72;
        break;
    case 13:
        /* First arithmetic domain failure occurs after earlier samples. */
        f->x[0x88] = 0x7fffff;
        f->x[0xd65] = 0x7fffff;
        break;
    case 14:
        f->x[0x87] = 0x800000;
        f->x[0x8d] = 0;
        f->x[0xd65] = 0x800000;
        break;
    case 15:
        f->cpu.pc = 0x1c0;
        f->cpu.pc_advance = 0;
        f->cpu.registers[DSP_REG_R2]++;
        f->cpu.registers[DSP_REG_A1] = 3;
        f->cpu.registers[DSP_REG_A2] = 0xff;
        break;
    case 16:
        break; /* Unsupported call size below. */
    case 17:
        f->cpu.registers[DSP_REG_N4] = 3;
        break;
    case 18:
        f->cpu.cycle_budget = 1;
        break;
    case 19:
        f->cpu.registers[DSP_REG_SSH] = 0;
        break;
    default:
        g_assert_not_reached();
    }
    Fixture *before = g_memdup2(f, sizeof(*f));
    g_assert_false(gp_filter_hle_run_verified(
        &f->cpu, f->x, f->y, GPOINTER_TO_UINT(opaque) == 16 ? 1 : 1000));
    g_assert_cmpmem(f, sizeof(*f), before, sizeof(*before));
    g_free(before);
    g_free(f);
}

static void test_unknown_program(void)
{
    Fixture *f = new_fixture(3);
    Fixture *before = g_memdup2(f, sizeof(*f));
    uint32_t program[DSP_PRAM_SIZE] = { 0 };
    GPFilterHLE *hle = gp_filter_hle_new();
    for (unsigned i = 0; i < 3; i++) {
        g_assert_false(
            gp_filter_hle_try_run(hle, &f->cpu, f->x, f->y, program, 1000));
        g_assert_cmpmem(f, sizeof(*f), before, sizeof(*before));
        program[DSP_PRAM_SIZE - 1]++;
    }
    gp_filter_hle_reset(hle);
    g_assert_false(
        gp_filter_hle_try_run(hle, &f->cpu, f->x, f->y, program, 1000));
    g_assert_cmpmem(f, sizeof(*f), before, sizeof(*before));
    gp_filter_hle_free(hle);
    g_free(before);
    g_free(f);
}

static void unexpected_scratch(void *opaque, uint8_t *ptr, uint32_t addr,
                               size_t len, bool dir)
{
    g_assert_not_reached();
}

static void unexpected_fifo(void *opaque, uint8_t *ptr, unsigned index,
                            size_t len, bool dir)
{
    g_assert_not_reached();
}

static void test_backend_fallback(void)
{
    g_setenv("XEMU_GP_FILTER_HLE", "1", true);
    g_config.audio.use_dsp_jit = true;
    DSPState *dsp = dsp_init(NULL, unexpected_scratch, unexpected_fifo, true);
    for (unsigned pass = 0; pass < 3; pass++) {
        if (pass == 1) {
            dsp_reset(dsp);
        } else if (pass == 2) {
            dsp_set_engine(dsp, false);
            dsp_set_engine(dsp, true);
            dsp_reset(dsp);
        }
        /* Own assembled control: movep #1,x:<<$ffffc4; nop. */
        dsp_write_memory(dsp, 'P', 0, 0x08f484);
        dsp_write_memory(dsp, 'P', 1, 1);
        dsp_write_memory(dsp, 'P', 2, 0);
        dsp_set_halt_requested(dsp, false);
        dsp_run(dsp, 1000);
        g_assert_true(dsp_get_halt_requested(dsp));
    }
    dsp_destroy(dsp);
    g_unsetenv("XEMU_GP_FILTER_HLE");
}

static void test_captured_frame(void)
{
    const char *path = g_getenv("XEMU_GP_FILTER_HLE_TEST_CAPTURE");
    if (!path) {
        g_test_skip("A private recognized GP frame is required");
        return;
    }
    g_setenv("XEMU_GP_FILTER_HLE", "1", true);
    g_config.audio.use_dsp_jit = true;
    Error *err = NULL;
    DSPReplayCase *data = dsp_replay_load_case(path, &err);
    g_assert_null(err);
    g_assert_nonnull(data);
    ffi_runs = 0;
    g_assert_true(dsp_replay_verify_case(data, &err));
    g_assert_null(err);
    /* Agreement alone could pass with every call falling back to the JIT. */
    g_assert_cmpuint(ffi_runs, <, data->calls);
    dsp_replay_case_free(data);
    g_unsetenv("XEMU_GP_FILTER_HLE");
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx-apu/gp-filter-hle/identity", test_identity);
    g_test_add_data_func("/mcpx-apu/gp-filter-hle/positive-limit",
                         GUINT_TO_POINTER(0), test_limit);
    g_test_add_data_func("/mcpx-apu/gp-filter-hle/negative-limit",
                         GUINT_TO_POINTER(1), test_limit);
    g_test_add_func("/mcpx-apu/gp-filter-hle/budget", test_budget);
    g_test_add_data_func("/mcpx-apu/gp-filter-hle/positive-normalization",
                         GUINT_TO_POINTER(0), test_normalization);
    g_test_add_data_func("/mcpx-apu/gp-filter-hle/negative-normalization",
                         GUINT_TO_POINTER(1), test_normalization);
    g_test_add_func("/mcpx-apu/gp-filter-hle/unknown-program",
                    test_unknown_program);
    g_test_add_func("/mcpx-apu/gp-filter-hle/backend-fallback",
                    test_backend_fallback);
    g_test_add_func("/mcpx-apu/gp-filter-hle/captured-frame",
                    test_captured_frame);
    for (unsigned i = 0; i < 20; i++) {
        g_autofree char *name =
            g_strdup_printf("/mcpx-apu/gp-filter-hle/reject-%u", i);
        g_test_add_data_func(name, GUINT_TO_POINTER(i), test_reject);
    }
    return g_test_run();
}
