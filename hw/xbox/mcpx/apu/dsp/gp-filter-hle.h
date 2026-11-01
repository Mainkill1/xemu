/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCPX_GP_FILTER_HLE_H
#define MCPX_GP_FILTER_HLE_H

#include <dsp56300.h>

/*
 * Internal kernel: caller has verified the complete program identity.
 * Returns false without changing CPU or memory if the call is unsupported.
 */
bool gp_filter_hle_run_verified(Dsp56300State *cpu, uint32_t *xram,
                                uint32_t *yram, int cycles);

#endif
