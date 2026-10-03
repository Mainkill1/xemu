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

static void test_zeta_dirty_matrix(void)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);

    for (unsigned int clearing = 0; clearing < 2; clearing++) {
        for (unsigned int accessed = 0; accessed < 2; accessed++) {
            for (unsigned int depth_test = 0; depth_test < 2; depth_test++) {
                for (unsigned int depth_write = 0; depth_write < 2;
                     depth_write++) {
                    for (unsigned int stencil_test = 0; stencil_test < 2;
                         stencil_test++) {
                        for (unsigned int stencil_write = 0;
                             stencil_write < 2; stencil_write++) {
                            bool draw_write =
                                (depth_test && depth_write) ||
                                (stencil_test && stencil_write);
                            bool expected = accessed && (clearing || draw_write);

                            pg->clearing = clearing;
                            pg->regs_[NV_PGRAPH_CONTROL_0] =
                                (depth_test ? NV_PGRAPH_CONTROL_0_ZENABLE : 0) |
                                (depth_write ?
                                     NV_PGRAPH_CONTROL_0_ZWRITEENABLE : 0) |
                                (stencil_write ?
                                     NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE :
                                     0);
                            pg->regs_[NV_PGRAPH_CONTROL_1] =
                                stencil_test ?
                                    NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE : 0;

                            g_assert_cmpint(
                                pgraph_zeta_surface_dirty_required(pg,
                                                                   accessed),
                                ==, expected);
                        }
                    }
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph/zeta/draw-write-gates",
                    test_depth_and_stencil_write_gates);
    g_test_add_func("/xbox/pgraph/zeta/dirty-matrix",
                    test_zeta_dirty_matrix);
    return g_test_run();
}
