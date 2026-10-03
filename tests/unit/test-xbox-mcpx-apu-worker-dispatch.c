/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include <SDL3/SDL.h>

static int host_cpus;
static int test_logical_cpus(void)
{
    return host_cpus;
}

/* Only CPU discovery is replaced; exercise actual VP initialization/queues. */
#define SDL_GetNumLogicalCPUCores test_logical_cpus
static int voice_samples_main(int argc, char **argv) G_GNUC_UNUSED;
#define main voice_samples_main
#include "test-xbox-mcpx-apu-voice-samples.c"
#undef main
#undef SDL_GetNumLogicalCPUCores

struct config g_config;
int g_dbg_voice_monitor = -1;

bool mcpx_apu_debug_is_muted(uint16_t voice)
{
    return false;
}

static void finalize_test_pool(void)
{
    mcpx_apu_vp_finalize(&d);
    VoiceWorkDispatch *vwd = &d.vp.voice_work_dispatch;
    qemu_cond_destroy(&vwd->work_pending);
    qemu_cond_destroy(&vwd->work_finished);
    qemu_mutex_destroy(&vwd->lock);
}

static void test_production_auto_initialization(void)
{
    const int cpus[] = { 1, 2, 3, 4, 8, 16 };
    const int requests[] = { 0, 1, 4, 16 };

    for (int linear = 0; linear < 2; linear++) {
        for (int c = 0; c < ARRAY_SIZE(cpus); c++) {
            for (int r = 0; r < ARRAY_SIZE(requests); r++) {
                setup_voice(64, false);
                host_cpus = cpus[c];
                g_config.audio.vp.resampler =
                    linear ? CONFIG_AUDIO_VP_RESAMPLER_LINEAR :
                             CONFIG_AUDIO_VP_RESAMPLER_SINC;
                g_config.audio.vp.num_workers = requests[r];
                mcpx_apu_vp_init(&d);
                int expected =
                    requests[r] ? requests[r] : MIN(cpus[c], linear ? 4 : 16);
                g_assert_cmpint(d.vp.voice_work_dispatch.num_workers, ==,
                                expected);
                g_assert_cmpint(d.vp.resampler_type, ==,
                                linear ? SRC_LINEAR : SRC_SINC_FASTEST);
                /* A settings edit cannot resize the running pool. */
                g_config.audio.vp.num_workers = 1;
                g_config.audio.vp.resampler = !linear;
                g_assert_cmpint(d.vp.voice_work_dispatch.num_workers, ==,
                                expected);
                g_assert_cmpint(d.vp.resampler_type, ==,
                                linear ? SRC_LINEAR : SRC_SINC_FASTEST);
                finalize_test_pool();
            }
        }
    }
}

/*
 * v2 is distinct from the historical 45-voice fixture: muted sends use bin 2.
 * Retain bin 31 as a grouped control rather than changing historic identity.
 */
static void prepare_voices(int workers, bool grouped)
{
    setup_voice(2048, false);
    uint8_t template[NV_PAVS_SIZE];
    memcpy(template, bytes + 0x1000, sizeof(template));
    int unused = grouped ? 31 : 2;
    d.regs[NV_PAPU_VPSGEADDR] = 0x9000;
    stl_le_phys(&address_space_memory, 0x9000, 0xa000);
    for (int i = 0; i < 2048; i++) {
        stw_le_phys(&address_space_memory, 0xa000 + i * 2, 4096);
    }
    for (int i = 0; i < 45; i++) {
        int v = 64 + i;
        memcpy(bytes + 0x1000 + v * NV_PAVS_SIZE, template, sizeof(template));
        stl_le_phys(&address_space_memory,
                    0x1000 + v * NV_PAVS_SIZE + NV_PAVS_VOICE_CFG_VBIN,
                    (1U << 5) | (unused << 10) | (unused << 16) |
                        (unused << 21) | (unused << 26));
        voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_LOOP,
                       1);
        voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_FMT,
                       NV_PAVS_VOICE_CFG_FMT_V6BIN, unused);
        voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_FMT,
                       NV_PAVS_VOICE_CFG_FMT_V7BIN, unused);
        stl_le_phys(&address_space_memory,
                    0x1000 + v * NV_PAVS_SIZE + NV_PAVS_VOICE_CFG_ENVA,
                    0xff000000);
        stl_le_phys(&address_space_memory,
                    0x1000 + v * NV_PAVS_SIZE + NV_PAVS_VOICE_CFG_ENVF,
                    0xff000000);
        voice_set_mask(&d, v, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_EACUR, 5);
        voice_set_mask(&d, v, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_EFCUR, 5);
        stl_le_phys(&address_space_memory,
                    0x1000 + v * NV_PAVS_SIZE + NV_PAVS_VOICE_TAR_VOLA,
                    0x000f000f);
        stl_le_phys(&address_space_memory,
                    0x1000 + v * NV_PAVS_SIZE + NV_PAVS_VOICE_TAR_VOLB,
                    0xffffffff);
        stl_le_phys(&address_space_memory,
                    0x1000 + v * NV_PAVS_SIZE + NV_PAVS_VOICE_TAR_VOLC,
                    0xffffffff);
    }
    g_config.audio.vp.resampler = CONFIG_AUDIO_VP_RESAMPLER_LINEAR;
    g_config.audio.vp.num_workers = workers;
    host_cpus = 16;
    d.monitor.point = MCPX_APU_DEBUG_MON_GP_OR_EP;
    d.vp.submix_headroom[0] = d.vp.submix_headroom[1] = 6;
    mcpx_apu_vp_init(&d);
}

static void test_queue_membership_and_mix(void)
{
    const int requests[] = { 0, 1, 2, 4, 8, 16 };

    for (int grouped = 0; grouped < 2; grouped++) {
        for (int r = 0; r < ARRAY_SIZE(requests); r++) {
            prepare_voices(requests[r], grouped);
            VoiceWorkDispatch *vwd = &d.vp.voice_work_dispatch;
            int workers = vwd->num_workers;
            bool seen[45] = { 0 };
            qemu_mutex_lock(&vwd->lock);
            for (int i = 0; i < 45; i++) {
                voice_work_enqueue(&d, 64 + i, 0);
            }
            voice_work_schedule(&d);
            for (int w = 0; w < workers; w++) {
                VoiceWorker *worker = &vwd->workers[w];
                int expected = grouped ? (w == 0 ? 45 : 0) :
                                         45 / workers + (w < 45 % workers);
                g_assert_cmpint(worker->queue_len, ==, expected);
                g_test_message(
                    "v2 grouped=%d requested=%d pool=%d worker=%d queue=%d",
                    grouped, requests[r], workers, w, worker->queue_len);
                for (int i = 0; i < worker->queue_len; i++) {
                    int id = worker->queue[i].voice - 64;
                    g_assert_cmpint(id, >=, 0);
                    g_assert_cmpint(id, <, 45);
                    g_assert_false(seen[id]);
                    seen[id] = true;
                }
                worker->queue_len = 0;
            }
            for (int i = 0; i < 45; i++) {
                g_assert_true(seen[i]);
            }
            vwd->workers_pending = 0;
            qemu_mutex_unlock(&vwd->lock);
            /* Execute the same queued voices through real worker threads. */
            for (int f = 0; f < 4; f++) {
                float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
                voice_work_dispatch(&d, bins);
                for (int b = 0; b < NUM_MIXBINS; b++) {
                    for (int s = 0; s < NUM_SAMPLES_PER_FRAME; s++) {
                        g_assert_cmpfloat_with_epsilon(
                            bins[b][s], b < 2 ? 45.0f / 512.0f : 0, 0.000001f);
                    }
                }
                for (int i = 0; i < 45; i++) {
                    voice_work_enqueue(&d, 64 + i, 0);
                }
            }
            vwd->queue_len = 0;
            finalize_test_pool();
        }
    }
}

static void test_unequal_multipass_groups(void)
{
    const int requests[] = { 1, 4, 16 };
    const int sizes[] = { 3, 2, 4, 1 };

    for (int r = 0; r < ARRAY_SIZE(requests); r++) {
        prepare_voices(requests[r], false);
        VoiceWorkDispatch *vwd = &d.vp.voice_work_dispatch;
        int id = 0;
        for (int group = 0; group < ARRAY_SIZE(sizes); group++) {
            for (int member = 0; member < sizes[group]; member++, id++) {
                int v = 64 + id;
                bool last = member == sizes[group] - 1;
                if (last) {
                    voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_FMT,
                                   NV_PAVS_VOICE_CFG_FMT_MULTIPASS, 1);
                    voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_FMT,
                                   NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN, 31);
                    voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_FMT,
                                   NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX, 1);
                } else {
                    voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_VBIN,
                                   NV_PAVS_VOICE_CFG_VBIN_V0BIN, 31);
                    voice_set_mask(&d, v, NV_PAVS_VOICE_CFG_VBIN,
                                   NV_PAVS_VOICE_CFG_VBIN_V1BIN, 31);
                }
                voice_work_enqueue(&d, v, 0);
            }
        }
        qemu_mutex_lock(&vwd->lock);
        voice_work_schedule(&d);
        bool seen[10] = { 0 };
        for (int w = 0; w < vwd->num_workers; w++) {
            VoiceWorker *worker = &vwd->workers[w];
            int expected = requests[r] == 1 ? 10 : w < 4 ? sizes[w] : 0;
            g_assert_cmpint(worker->queue_len, ==, expected);
            for (int i = 0; i < worker->queue_len; i++) {
                int voice = worker->queue[i].voice - 64;
                g_assert_cmpint(voice, >=, 0);
                g_assert_cmpint(voice, <, 10);
                g_assert_false(seen[voice]);
                seen[voice] = true;
            }
            worker->queue_len = 0;
        }
        for (int i = 0; i < 10; i++) {
            g_assert_true(seen[i]);
        }
        vwd->workers_pending = 0;
        qemu_mutex_unlock(&vwd->lock);
        float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };
        voice_work_dispatch(&d, bins);
        /* Six producers feed three cleared groups, then an empty group. */
        for (int b = 0; b < NUM_MIXBINS; b++) {
            for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                g_assert_cmpfloat_with_epsilon(
                    bins[b][i], b < 2 ? 6.0f / 256.0f : 0, 0.000001f);
            }
        }
        finalize_test_pool();
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    cpu_exec_init_all();
    rust_bql_mock_lock();
    init_memory();
    g_test_add_func("/xbox/apu/worker/production-auto-initialization",
                    test_production_auto_initialization);
    g_test_add_func("/xbox/apu/worker/v2-queue-membership-and-mix",
                    test_queue_membership_and_mix);
    g_test_add_func("/xbox/apu/worker/unequal-multipass-groups",
                    test_unequal_multipass_groups);
    return g_test_run();
}
