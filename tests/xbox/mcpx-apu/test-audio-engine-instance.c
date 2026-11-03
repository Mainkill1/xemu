/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "hw/xbox/mcpx/apu/audio-engine-policy.h"

static void check_new_hle_instance(void)
{
    MCPXAPUState *d = g_new0(MCPXAPUState, 1);
    d->audio_engine = AUDIO_ENGINE_HLE;
    mcpx_apu_dsp_init(d);
    g_assert_cmpint(d->monitor.point, ==, MCPX_APU_DEBUG_MON_GP_OR_EP);
    g_assert_true(d->gp.realtime);
    g_assert_true(d->ep.realtime);
    g_assert_nonnull(d->ep.mix_observer);
    mcpx_apu_dsp_finalize(d);
    dsp_destroy(d->gp.dsp);
    dsp_destroy(d->ep.dsp);
    g_free(d);
}

static void check_jit_instance(void)
{
    MCPXAPUState *d = g_new0(MCPXAPUState, 1);
    d->audio_engine = AUDIO_ENGINE_JIT;
    mcpx_apu_dsp_init(d);
    g_assert_true(d->gp.realtime);
    g_assert_true(d->ep.realtime);
    g_assert_null(d->ep.mix_observer);
    mcpx_apu_dsp_finalize(d);
    dsp_destroy(d->gp.dsp);
    dsp_destroy(d->ep.dsp);
    g_free(d);
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    g_unsetenv("XEMU_EP_NATIVE_EXPERIMENT");
    g_unsetenv("XEMU_EP_MIX_SHADOW");
    g_config.audio.use_dsp = false;
    g_config.audio.use_dsp_jit = false;
    check_new_hle_instance();
    check_new_hle_instance();
    check_jit_instance();
    return 0;
}
