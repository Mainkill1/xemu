/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_EFFECT_SUPPRESSION_H
#define HW_XBOX_NV2A_PGRAPH_EFFECT_SUPPRESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PGRAPHState PGRAPHState;

uint64_t pgraph_vertex_program_fingerprint(const uint32_t *words,
                                           size_t word_count);
bool pgraph_matches_issue149_effect_signature(PGRAPHState *pg,
                                              size_t element_count,
                                              uint32_t min_element,
                                              uint32_t max_element,
                                              uint64_t vsh_fingerprint);
bool pgraph_matches_issue149_effect(PGRAPHState *pg, size_t element_count,
                                    uint32_t min_element,
                                    uint32_t max_element);

#endif
