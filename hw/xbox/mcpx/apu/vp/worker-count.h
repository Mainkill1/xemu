/* SPDX-License-Identifier: LGPL-2.0-or-later */

#ifndef HW_XBOX_MCPX_APU_VP_WORKER_COUNT_H
#define HW_XBOX_MCPX_APU_VP_WORKER_COUNT_H

#define MCPX_APU_MAX_VOICE_WORKERS 16
#define MCPX_APU_AUTO_VOICE_WORKERS 4

static inline int mcpx_apu_voice_worker_count(int configured_workers,
                                            int logical_cpus)
{
    int workers = configured_workers != 0 ? configured_workers : logical_cpus;

    if (configured_workers == 0 && workers > MCPX_APU_AUTO_VOICE_WORKERS) {
        workers = MCPX_APU_AUTO_VOICE_WORKERS;
    }
    if (workers < 1) {
        return 1;
    }
    if (workers > MCPX_APU_MAX_VOICE_WORKERS) {
        return MCPX_APU_MAX_VOICE_WORKERS;
    }
    return workers;
}

#endif
