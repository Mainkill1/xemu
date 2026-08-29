/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>

#include "hw/xbox/nv2a/pgraph/uniform-source.h"
#include "hw/xbox/nv2a/pgraph/uniform-stage-update.h"

static void test_stage_epochs_are_independent(void)
{
    PGRAPHUniformSourceEpochs epochs = { 0 };
    PGRAPHUniformSourceEpochs previous = epochs;

    pgraph_uniform_source_touch(&epochs, PGRAPH_UNIFORM_STAGE_MASK_PSH);

    g_assert_false(pgraph_uniform_source_stage_changed(
        &epochs, &previous, PGRAPH_UNIFORM_STAGE_VSH));
    g_assert_true(pgraph_uniform_source_stage_changed(
        &epochs, &previous, PGRAPH_UNIFORM_STAGE_PSH));
    g_assert_cmpuint(epochs.total, ==, 1);
    g_assert_cmpuint(epochs.unclassified, ==, 0);
}

static void test_both_and_unclassified_are_conservative(void)
{
    PGRAPHUniformSourceEpochs epochs = { 0 };

    pgraph_uniform_source_touch(&epochs, PGRAPH_UNIFORM_STAGE_MASK_BOTH);
    g_assert_cmpuint(epochs.stage[PGRAPH_UNIFORM_STAGE_VSH], ==, 1);
    g_assert_cmpuint(epochs.stage[PGRAPH_UNIFORM_STAGE_PSH], ==, 1);
    g_assert_cmpuint(epochs.unclassified, ==, 0);

    pgraph_uniform_source_touch_unclassified(&epochs);
    g_assert_cmpuint(epochs.stage[PGRAPH_UNIFORM_STAGE_VSH], ==, 2);
    g_assert_cmpuint(epochs.stage[PGRAPH_UNIFORM_STAGE_PSH], ==, 2);
    g_assert_cmpuint(epochs.unclassified, ==, 1);
}

static void assert_register_range(unsigned int first, unsigned int count,
                                  PGRAPHUniformStageMask expected)
{
    for (unsigned int i = 0; i < count; i++) {
        g_assert_cmpuint(
            pgraph_reg_uniform_stage_mask(first + i * 4, UINT32_MAX), ==,
            expected);
    }
}

static void test_register_stage_classification(void)
{
    assert_register_range(NV_PGRAPH_COMBINEFACTOR0, 8,
                          PGRAPH_UNIFORM_STAGE_MASK_PSH);
    assert_register_range(NV_PGRAPH_COMBINEFACTOR1, 8,
                          PGRAPH_UNIFORM_STAGE_MASK_PSH);
    assert_register_range(NV_PGRAPH_WINDOWCLIPX0, 8,
                          PGRAPH_UNIFORM_STAGE_MASK_PSH);
    assert_register_range(NV_PGRAPH_WINDOWCLIPY0, 8,
                          PGRAPH_UNIFORM_STAGE_MASK_PSH);
    assert_register_range(NV_PGRAPH_TEXFMT0, 4,
                          PGRAPH_UNIFORM_STAGE_MASK_PSH);

    g_assert_cmpuint(
        pgraph_reg_uniform_stage_mask(NV_PGRAPH_FOGPARAM0, UINT32_MAX), ==,
        PGRAPH_UNIFORM_STAGE_MASK_VSH);
    g_assert_cmpuint(
        pgraph_reg_uniform_stage_mask(NV_PGRAPH_ZCLIPMIN, UINT32_MAX), ==,
        PGRAPH_UNIFORM_STAGE_MASK_BOTH);
    g_assert_cmpuint(
        pgraph_reg_uniform_stage_mask(NV_PGRAPH_CONTROL_0, UINT32_MAX), ==,
        PGRAPH_UNIFORM_STAGE_MASK_PSH);
    g_assert_cmpuint(
        pgraph_reg_uniform_stage_mask(
            NV_PGRAPH_SETUPRASTER,
            NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE), ==,
        PGRAPH_UNIFORM_STAGE_MASK_PSH);
    g_assert_cmpuint(
        pgraph_reg_uniform_stage_mask(
            NV_PGRAPH_SETUPRASTER, NV_PGRAPH_SETUPRASTER_Z_FORMAT), ==,
        PGRAPH_UNIFORM_STAGE_MASK_BOTH);
    g_assert_cmpuint(
        pgraph_reg_uniform_stage_mask(NV_PGRAPH_BLEND, UINT32_MAX), ==,
        PGRAPH_UNIFORM_STAGE_MASK_NONE);
}

static void test_stage_update_decisions(void)
{
    PGRAPHUniformStageUpdateInputs inputs = { 0 };
    bool update_stage[PGRAPH_UNIFORM_STAGE_COUNT];

    inputs.source_changed[PGRAPH_UNIFORM_STAGE_PSH] = true;
    pgraph_uniform_stage_update_needs(&inputs, update_stage);
    g_assert_false(update_stage[PGRAPH_UNIFORM_STAGE_VSH]);
    g_assert_true(update_stage[PGRAPH_UNIFORM_STAGE_PSH]);

    inputs = (PGRAPHUniformStageUpdateInputs){ 0 };
    inputs.texture_bindings_changed = true;
    pgraph_uniform_stage_update_needs(&inputs, update_stage);
    g_assert_false(update_stage[PGRAPH_UNIFORM_STAGE_VSH]);
    g_assert_true(update_stage[PGRAPH_UNIFORM_STAGE_PSH]);

    inputs = (PGRAPHUniformStageUpdateInputs){ 0 };
    inputs.vsh_rows_dirty = true;
    pgraph_uniform_stage_update_needs(&inputs, update_stage);
    g_assert_true(update_stage[PGRAPH_UNIFORM_STAGE_VSH]);
    g_assert_false(update_stage[PGRAPH_UNIFORM_STAGE_PSH]);

    inputs = (PGRAPHUniformStageUpdateInputs){ 0 };
    inputs.force_full_update = true;
    pgraph_uniform_stage_update_needs(&inputs, update_stage);
    g_assert_true(update_stage[PGRAPH_UNIFORM_STAGE_VSH]);
    g_assert_true(update_stage[PGRAPH_UNIFORM_STAGE_PSH]);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/pgraph/uniform-source/stage-epochs",
                    test_stage_epochs_are_independent);
    g_test_add_func("/xbox/pgraph/uniform-source/conservative-unknown",
                    test_both_and_unclassified_are_conservative);
    g_test_add_func("/xbox/pgraph/uniform-source/register-classification",
                    test_register_stage_classification);
    g_test_add_func("/xbox/pgraph/uniform-source/stage-update-decisions",
                    test_stage_update_decisions);

    return g_test_run();
}
