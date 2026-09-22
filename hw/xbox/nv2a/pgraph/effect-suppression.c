/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "effect-suppression.h"
#include "pgraph.h"

/*
 * Provisional compatibility signature for Mainkill1/xemu#149.  This is
 * intentionally stricter than a shader identity: the program, pixel/depth
 * state, primitive shape, element count, and element span must all match the
 * captured failing draw.  It may be removed once the missing NV2A behavior is
 * understood.
 */
#define ISSUE149_VSH_INSTRUCTION_COUNT 20
#define ISSUE149_VSH_FINGERPRINT UINT64_C(0x0df7d01b652406a4)

uint64_t pgraph_vertex_program_fingerprint(const uint32_t *words,
                                           size_t word_count)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);

    for (size_t i = 0; i < word_count; i++) {
        uint32_t word = words[i];
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            hash ^= (word >> shift) & 0xff;
            hash *= UINT64_C(0x100000001b3);
        }
    }
    return hash;
}

bool pgraph_matches_issue149_effect_signature(PGRAPHState *pg,
                                              size_t element_count,
                                              uint32_t min_element,
                                              uint32_t max_element,
                                              uint64_t vsh_fingerprint)
{
    if (element_count != 217 || max_element - min_element != 91 ||
        pg->primitive_mode != PRIM_TYPE_TRIANGLE_STRIP ||
        pgraph_reg_r(pg, NV_PGRAPH_BLEND) != 0x00000542 ||
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0) != 0x3f034400 ||
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1) != 0xffff0020 ||
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2) != 0x00000111 ||
        pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) != 0x10200000 ||
        pgraph_reg_r(pg, NV_PGRAPH_COMBINECTL) != 0x00011105 ||
        pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG0) != 0x0000000c ||
        pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG1) != 0x00001c80 ||
        pgraph_reg_r(pg, NV_PGRAPH_SHADERPROG) != 0x00000421 ||
        pgraph_reg_r(pg, NV_PGRAPH_SHADERCTL) != 0 ||
        pgraph_reg_r(pg, NV_PGRAPH_SHADERCLIPMODE) != 0 ||
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CSV0_D),
                 NV_PGRAPH_CSV0_D_MODE) != 2 ||
        vsh_fingerprint != ISSUE149_VSH_FINGERPRINT) {
        return false;
    }
    return true;
}

bool pgraph_matches_issue149_effect(PGRAPHState *pg, size_t element_count,
                                    uint32_t min_element,
                                    uint32_t max_element)
{
    unsigned int start = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CSV0_C),
                                  NV_PGRAPH_CSV0_C_CHEOPS_PROGRAM_START);
    if (start + ISSUE149_VSH_INSTRUCTION_COUNT >
        NV2A_MAX_TRANSFORM_PROGRAM_LENGTH) {
        return false;
    }
    uint64_t fingerprint = pgraph_vertex_program_fingerprint(
        &pg->program_data[start][0],
        ISSUE149_VSH_INSTRUCTION_COUNT * VSH_TOKEN_SIZE);
    return pgraph_matches_issue149_effect_signature(
        pg, element_count, min_element, max_element, fingerprint);
}
