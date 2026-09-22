/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "effect-suppression.h"
#include "pgraph.h"
#include "hw/xbox/nv2a/trace.h"

/*
 * Provisional compatibility signature for Mainkill1/xemu#149.  This is
 * intentionally stricter than a shader identity: the program, pixel/depth
 * state, primitive shape, element count, and element range must all match one
 * of the captured failing draws.  It may be removed once the missing NV2A
 * behavior is understood.
 */
#define ISSUE149_VSH_INSTRUCTION_COUNT 20
#define ISSUE149_VSH_FINGERPRINT UINT64_C(0x0df7d01b652406a4)

typedef struct Issue149ProblemDraw {
    size_t element_count;
    uint32_t min_element;
    uint32_t max_element;
} Issue149ProblemDraw;

/*
 * RenderDoc draw-call overlays identify these four members of the otherwise
 * valid shader family as the screen-covering geometry.  The ranges are index
 * values, not guest addresses, and were stable across every captured frame.
 */
static const Issue149ProblemDraw issue149_problem_draws[] = {
    { 217, 5005, 5096 },
    { 165, 4881, 4945 },
    { 142, 4946, 5004 },
    { 126, 5027, 5393 },
};

static bool issue149_matches_problem_draw_shape(size_t element_count,
                                                uint32_t min_element,
                                                uint32_t max_element)
{
    for (size_t i = 0; i < ARRAY_SIZE(issue149_problem_draws); i++) {
        const Issue149ProblemDraw *draw = &issue149_problem_draws[i];

        if (element_count == draw->element_count &&
            min_element == draw->min_element &&
            max_element == draw->max_element) {
            return true;
        }
    }
    return false;
}

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

static uint32_t issue149_normalize_control_0(uint32_t control_0)
{
    if (!(control_0 & NV_PGRAPH_CONTROL_0_ALPHATESTENABLE)) {
        control_0 &= ~(NV_PGRAPH_CONTROL_0_ALPHAREF |
                       NV_PGRAPH_CONTROL_0_ALPHAFUNC);
    }
    return control_0;
}

uint32_t pgraph_issue149_effect_signature_mismatches(
    PGRAPHState *pg, size_t element_count, uint32_t min_element,
    uint32_t max_element, uint64_t vsh_fingerprint)
{
    uint32_t mismatches = 0;
    bool known_element_count = false;

    for (size_t i = 0; i < ARRAY_SIZE(issue149_problem_draws); i++) {
        known_element_count |=
            element_count == issue149_problem_draws[i].element_count;
    }

#define CHECK_ISSUE149_SIGNATURE(condition, bit) \
    do {                                          \
        if (!(condition)) {                       \
            mismatches |= (bit);                  \
        }                                         \
    } while (0)

    CHECK_ISSUE149_SIGNATURE(known_element_count,
                             ISSUE149_MISMATCH_ELEMENT_COUNT);
    if (known_element_count) {
        CHECK_ISSUE149_SIGNATURE(
            issue149_matches_problem_draw_shape(element_count, min_element,
                                                max_element),
            ISSUE149_MISMATCH_ELEMENT_RANGE);
    }
    CHECK_ISSUE149_SIGNATURE(pg->primitive_mode == PRIM_TYPE_TRIANGLE_STRIP,
                             ISSUE149_MISMATCH_PRIMITIVE);
    CHECK_ISSUE149_SIGNATURE(pgraph_reg_r(pg, NV_PGRAPH_BLEND) == 0x00000542,
                             ISSUE149_MISMATCH_BLEND);
    CHECK_ISSUE149_SIGNATURE(
        issue149_normalize_control_0(
            pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0)) ==
            issue149_normalize_control_0(0x3f034400),
        ISSUE149_MISMATCH_CONTROL_0);
    CHECK_ISSUE149_SIGNATURE(
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1) == 0xffff0020,
        ISSUE149_MISMATCH_CONTROL_1);
    CHECK_ISSUE149_SIGNATURE(
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2) == 0x00000111,
        ISSUE149_MISMATCH_CONTROL_2);
    CHECK_ISSUE149_SIGNATURE(
        pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) == 0x10200000,
        ISSUE149_MISMATCH_SETUPRASTER);
    CHECK_ISSUE149_SIGNATURE(
        pgraph_reg_r(pg, NV_PGRAPH_COMBINECTL) == 0x00011105,
        ISSUE149_MISMATCH_COMBINECTL);
    CHECK_ISSUE149_SIGNATURE(
        pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG0) == 0x0000000c,
        ISSUE149_MISMATCH_COMBINESPECFOG0);
    CHECK_ISSUE149_SIGNATURE(
        pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG1) == 0x00001c80,
        ISSUE149_MISMATCH_COMBINESPECFOG1);
    CHECK_ISSUE149_SIGNATURE(
        pgraph_reg_r(pg, NV_PGRAPH_SHADERPROG) == 0x00000421,
        ISSUE149_MISMATCH_SHADERPROG);
    CHECK_ISSUE149_SIGNATURE(pgraph_reg_r(pg, NV_PGRAPH_SHADERCTL) == 0,
                             ISSUE149_MISMATCH_SHADERCTL);
    CHECK_ISSUE149_SIGNATURE(pgraph_reg_r(pg, NV_PGRAPH_SHADERCLIPMODE) == 0,
                             ISSUE149_MISMATCH_SHADERCLIPMODE);
    CHECK_ISSUE149_SIGNATURE(
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CSV0_D),
                 NV_PGRAPH_CSV0_D_MODE) == 2,
        ISSUE149_MISMATCH_VSH_MODE);
    CHECK_ISSUE149_SIGNATURE(vsh_fingerprint == ISSUE149_VSH_FINGERPRINT,
                             ISSUE149_MISMATCH_VSH_FINGERPRINT);

#undef CHECK_ISSUE149_SIGNATURE

    return mismatches;
}

bool pgraph_matches_issue149_effect_signature(PGRAPHState *pg,
                                              size_t element_count,
                                              uint32_t min_element,
                                              uint32_t max_element,
                                              uint64_t vsh_fingerprint)
{
    return pgraph_issue149_effect_signature_mismatches(
               pg, element_count, min_element, max_element, vsh_fingerprint) ==
           0;
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
    uint32_t mismatches = pgraph_issue149_effect_signature_mismatches(
        pg, element_count, min_element, max_element, fingerprint);
    bool candidate_shape = issue149_matches_problem_draw_shape(
                               element_count, min_element, max_element) ||
                           fingerprint == ISSUE149_VSH_FINGERPRINT;
    if (candidate_shape) {
        trace_nv2a_pgraph_issue149_signature(
            mismatches, element_count, min_element, max_element, fingerprint,
            start, pg->primitive_mode,
            GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CSV0_D),
                     NV_PGRAPH_CSV0_D_MODE));
        trace_nv2a_pgraph_issue149_state0(
            pgraph_reg_r(pg, NV_PGRAPH_BLEND),
            pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0),
            pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
            pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2),
            pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
            pgraph_reg_r(pg, NV_PGRAPH_COMBINECTL));
        trace_nv2a_pgraph_issue149_state1(
            pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG0),
            pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG1),
            pgraph_reg_r(pg, NV_PGRAPH_SHADERPROG),
            pgraph_reg_r(pg, NV_PGRAPH_SHADERCTL),
            pgraph_reg_r(pg, NV_PGRAPH_SHADERCLIPMODE));
    }
    return mismatches == 0;
}
