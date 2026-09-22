/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_EFFECT_SUPPRESSION_H
#define HW_XBOX_NV2A_PGRAPH_EFFECT_SUPPRESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PGRAPHState PGRAPHState;

enum {
    ISSUE149_MISMATCH_ELEMENT_COUNT = 1u << 0,
    ISSUE149_MISMATCH_ELEMENT_RANGE = 1u << 1,
    ISSUE149_MISMATCH_PRIMITIVE = 1u << 2,
    ISSUE149_MISMATCH_BLEND = 1u << 3,
    ISSUE149_MISMATCH_CONTROL_0 = 1u << 4,
    ISSUE149_MISMATCH_CONTROL_1 = 1u << 5,
    ISSUE149_MISMATCH_CONTROL_2 = 1u << 6,
    ISSUE149_MISMATCH_SETUPRASTER = 1u << 7,
    ISSUE149_MISMATCH_COMBINECTL = 1u << 8,
    ISSUE149_MISMATCH_COMBINESPECFOG0 = 1u << 9,
    ISSUE149_MISMATCH_COMBINESPECFOG1 = 1u << 10,
    ISSUE149_MISMATCH_SHADERPROG = 1u << 11,
    ISSUE149_MISMATCH_SHADERCTL = 1u << 12,
    ISSUE149_MISMATCH_SHADERCLIPMODE = 1u << 13,
    ISSUE149_MISMATCH_VSH_MODE = 1u << 14,
    ISSUE149_MISMATCH_VSH_FINGERPRINT = 1u << 15,
};

uint64_t pgraph_vertex_program_fingerprint(const uint32_t *words,
                                           size_t word_count);
uint32_t pgraph_issue149_effect_signature_mismatches(
    PGRAPHState *pg, size_t element_count, uint32_t min_element,
    uint32_t max_element, uint64_t vsh_fingerprint);
bool pgraph_matches_issue149_effect_signature(PGRAPHState *pg,
                                              size_t element_count,
                                              uint32_t min_element,
                                              uint32_t max_element,
                                              uint64_t vsh_fingerprint);
bool pgraph_matches_issue149_effect(PGRAPHState *pg, size_t element_count,
                                    uint32_t min_element,
                                    uint32_t max_element);

#endif
