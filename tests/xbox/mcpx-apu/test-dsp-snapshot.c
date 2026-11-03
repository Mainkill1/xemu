/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/dsp.h"
#include "hw/xbox/mcpx/apu/dsp/dsp-jit-state.h"
#include "ui/xemu-settings.h"

static unsigned invalidate_count;
void __real_dsp56300_invalidate_cache(Dsp56300Jit *dsp);
void __wrap_dsp56300_invalidate_cache(Dsp56300Jit *dsp)
{
    invalidate_count++;
    __real_dsp56300_invalidate_cache(dsp);
}

static void unexpected_scratch(void *opaque, uint8_t *ptr, uint32_t addr,
                               size_t len, bool dir)
{
    g_assert_not_reached();
}

static void unexpected_fifo(void *opaque, uint8_t *ptr, unsigned int index,
                            size_t len, bool dir)
{
    g_assert_not_reached();
}

static void test_export(gconstpointer opaque)
{
    unsigned scenario = GPOINTER_TO_UINT(opaque);
    bool jit = scenario >= 3;
    unsigned polls = scenario % 3;
    g_config.audio.use_dsp_jit = jit;
    DSPState *dsp = dsp_init(NULL, unexpected_scratch, unexpected_fifo, true);

    dsp->dma.control = DMA_CONTROL_RUNNING;
    dsp->dma.dma_read_count = polls;
    dsp->dma.configuration = 0xff000123;
    dsp->dma.start_block = 0x456;
    dsp->dma.next_block = 0x789;
    dsp->dma.eol = true;
    dsp->interrupts = 3;

    for (unsigned i = 0; i < 2; i++) {
        dsp_sync_to_vm(dsp);
        g_assert_cmphex(dsp->dma.control, ==, DMA_CONTROL_RUNNING);
        g_assert_cmpuint(dsp->dma.dma_read_count, ==, polls);
        g_assert_cmphex(dsp->dma.configuration, ==, 0xff000123);
        g_assert_true(dsp->dma.eol);
        g_assert_cmphex(dsp->interrupts, ==, 3);
        if (jit) {
            g_assert_cmphex(dsp->core.periph[0x33], ==, 0);
            g_assert_cmphex(dsp->core.periph[0x45], ==, 0x83);
            g_assert_cmphex(dsp->core.periph[0x54], ==, 0x789);
            g_assert_cmphex(dsp->core.periph[0x55], ==, 0x456);
            g_assert_cmphex(dsp->core.periph[0x56], ==, DMA_CONTROL_RUNNING);
            g_assert_cmphex(dsp->core.periph[0x57], ==, 0x123);
            g_assert_cmphex(dsp->core.periph[0], ==, 0xababa);
        }
    }

    /* Export must not steal any of the guest's remaining completion polls. */
    for (unsigned i = polls; i < 2; i++) {
        g_assert_cmphex(dsp_read_memory(dsp, 'X', 0xffffd6), ==,
                        DMA_CONTROL_RUNNING);
    }
    g_assert_cmphex(dsp_read_memory(dsp, 'X', 0xffffd6), ==,
                    DMA_CONTROL_STOPPED);
    g_assert_cmpuint(dsp->dma.dma_read_count, ==, 0);
    dsp_destroy(dsp);
}

static void test_native_data_restore(void)
{
    g_config.audio.use_dsp_jit = true;
    DSPState *dsp = dsp_init(NULL, unexpected_scratch, unexpected_fifo, true);
    g_autofree DSPJitState *state = g_new0(DSPJitState, 1);
    g_autofree DSPJitState *actual = g_new0(DSPJitState, 1);
    g_assert_true(dsp_jit_get_native_state(dsp, state));
    unsigned before = invalidate_count;
    state->xram[5] = 0x123456;
    g_assert_true(dsp_jit_set_native_data_state(dsp, state));
    g_assert_cmpuint(invalidate_count, ==, before);
    g_assert_true(dsp_jit_get_native_state(dsp, actual));
    g_assert_cmphex(actual->xram[5], ==, 0x123456);
    state->pram[2] ^= 1;
    state->xram[5] = 0xabcdef;
    g_assert_false(dsp_jit_set_native_data_state(dsp, state));
    g_assert_cmpuint(invalidate_count, ==, before);
    g_assert_true(dsp_jit_get_native_state(dsp, actual));
    g_assert_cmphex(actual->xram[5], ==, 0x123456);
    g_assert_true(dsp_jit_set_native_state(dsp, state));
    g_assert_cmpuint(invalidate_count, ==, before + 1);
    dsp_destroy(dsp);
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mcpx-apu/dsp-export/native-data-restore",
                    test_native_data_restore);
    for (unsigned i = 0; i < 6; i++) {
        g_autofree char *name = g_strdup_printf(
            "/mcpx-apu/dsp-export/%s-polls-%u", i >= 3 ? "jit" : "c", i % 3);
        g_test_add_data_func(name, GUINT_TO_POINTER(i), test_export);
    }
    return g_test_run();
}
