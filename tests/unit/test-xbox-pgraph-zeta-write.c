/*
 * NV2A draw write-state regression tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/pgraph.h"

static void test_depth_and_stencil_write_gates(void)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);

    pg->regs_[NV_PGRAPH_CONTROL_0] =
        NV_PGRAPH_CONTROL_0_ZENABLE |
        NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE;
    pg->regs_[NV_PGRAPH_CONTROL_1] = 0;

    /* Conker leaves the stencil write bit set while its tiny depth-tested
     * passes have depth writes disabled and stencil testing disabled. */
    g_assert_true(pgraph_zeta_write_enabled(pg));
    g_assert_false(pgraph_zeta_draw_write_enabled(pg));

    pg->regs_[NV_PGRAPH_CONTROL_0] |= NV_PGRAPH_CONTROL_0_ZWRITEENABLE;
    g_assert_true(pgraph_zeta_draw_write_enabled(pg));

    pg->regs_[NV_PGRAPH_CONTROL_0] &= ~NV_PGRAPH_CONTROL_0_ZENABLE;
    g_assert_false(pgraph_zeta_draw_write_enabled(pg));

    pg->regs_[NV_PGRAPH_CONTROL_1] =
        NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
    g_assert_true(pgraph_zeta_draw_write_enabled(pg));

    pg->regs_[NV_PGRAPH_CONTROL_0] &=
        ~NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE;
    g_assert_false(pgraph_zeta_draw_write_enabled(pg));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph/zeta/draw-write-gates",
                    test_depth_and_stencil_write_gates);
    return g_test_run();
}
