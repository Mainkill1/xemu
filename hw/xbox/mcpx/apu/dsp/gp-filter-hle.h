/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCPX_GP_FILTER_HLE_H
#define MCPX_GP_FILTER_HLE_H

#include <dsp56300.h>

typedef struct GPFilterHLE GPFilterHLE;

GPFilterHLE *gp_filter_hle_new(void);
void gp_filter_hle_free(GPFilterHLE *hle);
void gp_filter_hle_reset(GPFilterHLE *hle);
bool gp_filter_hle_try_run(GPFilterHLE *hle, Dsp56300State *cpu, uint32_t *xram,
                           uint32_t *yram, const uint32_t *pram, int cycles);

/*
 * Internal kernel: caller has verified the complete program identity.
 * Returns false without changing CPU or memory if the call is unsupported.
 */
bool gp_filter_hle_run_verified(Dsp56300State *cpu, uint32_t *xram,
                                uint32_t *yram, int cycles);

/* Offline projection of one final DO iteration; budget is unchanged. */
bool gp_filter_hle_finish_verified(Dsp56300State *cpu, uint32_t *xram,
                                   uint32_t *yram);

#endif
