/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/effect-suppression.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"

#define MATCHING_VSH_FINGERPRINT UINT64_C(0x0df7d01b652406a4)

static void matching_state(PGRAPHState *pg)
{
    memset(pg, 0, sizeof(*pg));
    pg->primitive_mode = PRIM_TYPE_TRIANGLE_STRIP;
    SET_MASK(pg->regs_[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_MODE, 2);
    pg->regs_[NV_PGRAPH_BLEND] = 0x00000542;
    pg->regs_[NV_PGRAPH_CONTROL_0] = 0x3f034400;
    pg->regs_[NV_PGRAPH_CONTROL_1] = 0xffff0020;
    pg->regs_[NV_PGRAPH_CONTROL_2] = 0x00000111;
    pg->regs_[NV_PGRAPH_SETUPRASTER] = 0x10200000;
    pg->regs_[NV_PGRAPH_COMBINECTL] = 0x00011105;
    pg->regs_[NV_PGRAPH_COMBINESPECFOG0] = 0x0000000c;
    pg->regs_[NV_PGRAPH_COMBINESPECFOG1] = 0x00001c80;
    pg->regs_[NV_PGRAPH_SHADERPROG] = 0x00000421;
    pg->regs_[NV_PGRAPH_SHADERCTL] = 0;
    pg->regs_[NV_PGRAPH_SHADERCLIPMODE] = 0;
}

int main(void)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    matching_state(pg);
    g_assert_true(pgraph_matches_issue149_effect_signature(
        pg, 217, 5005, 5096, MATCHING_VSH_FINGERPRINT));
    g_assert_false(pgraph_matches_issue149_effect_signature(
        pg, 216, 5005, 5096, MATCHING_VSH_FINGERPRINT));
    g_assert_false(pgraph_matches_issue149_effect_signature(
        pg, 217, 5005, 5095, MATCHING_VSH_FINGERPRINT));
    pg->primitive_mode = PRIM_TYPE_TRIANGLES;
    g_assert_false(pgraph_matches_issue149_effect_signature(
        pg, 217, 5005, 5096, MATCHING_VSH_FINGERPRINT));
    matching_state(pg);
    pg->regs_[NV_PGRAPH_CONTROL_0] ^= NV_PGRAPH_CONTROL_0_ZWRITEENABLE;
    g_assert_false(pgraph_matches_issue149_effect_signature(
        pg, 217, 5005, 5096, MATCHING_VSH_FINGERPRINT));
    matching_state(pg);
    g_assert_false(pgraph_matches_issue149_effect_signature(
        pg, 217, 5005, 5096, MATCHING_VSH_FINGERPRINT ^ 1));
    const uint32_t synthetic_words[] = { 0, 1, 0x12345678, 0xffffffff };
    g_assert_cmphex(pgraph_vertex_program_fingerprint(
                        synthetic_words, ARRAY_SIZE(synthetic_words)), ==,
                    UINT64_C(0xa368ff390283d8b8));
    g_free(pg);
    puts("PASS: issue #149 suppression requires the complete captured signature");
    return 0;
}
