/*
 * Deterministic NV2A DMA descriptor and report-range tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/host-utils.h"

#include "hw/xbox/nv2a/dma.h"
#include "hw/xbox/nv2a/nv2a_regs.h"

#define TEST_RAMIN_SIZE 32

static void write_dma_object(uint8_t *ramin, hwaddr address, uint32_t flags,
                             uint32_t limit, uint32_t frame)
{
    stl_le_p(ramin + address, flags);
    stl_le_p(ramin + address + 4, limit);
    stl_le_p(ramin + address + 8, frame);
}

static uint32_t report_dma_flags(unsigned int dma_class,
                                 unsigned int dma_target,
                                 unsigned int adjust)
{
    uint32_t flags = dma_class;

    SET_MASK(flags, NV_DMA_TARGET, dma_target);
    SET_MASK(flags, NV_DMA_ADJUST, adjust);
    return flags;
}

static void test_descriptor_extent(void)
{
    uint8_t ramin[TEST_RAMIN_SIZE] = { 0 };
    DMAObject dma;

    write_dma_object(ramin, TEST_RAMIN_SIZE - 12,
                     report_dma_flags(NV_DMA_IN_MEMORY_CLASS, 0, 0),
                     0x3f, 0x1000);
    g_assert_true(nv_dma_load_from_ramin_checked(
        ramin, sizeof(ramin), TEST_RAMIN_SIZE - 12, &dma));
    g_assert_cmphex(dma.dma_class, ==, NV_DMA_IN_MEMORY_CLASS);
    g_assert_cmphex(dma.address, ==, 0x1000);
    g_assert_cmphex(dma.limit, ==, 0x3f);

    g_assert_false(nv_dma_load_from_ramin_checked(
        ramin, sizeof(ramin), TEST_RAMIN_SIZE - 11, &dma));
    g_assert_false(nv_dma_load_from_ramin_checked(
        ramin, sizeof(ramin), sizeof(ramin) + 1, &dma));
    g_assert_false(nv_dma_load_from_ramin_checked(
        ramin, sizeof(ramin), HWADDR_MAX, &dma));
}

static void test_descriptor_snapshot(void)
{
    uint8_t ramin[TEST_RAMIN_SIZE] = { 0 };
    DMAObject queued, rewritten;

    write_dma_object(ramin, 0,
                     report_dma_flags(NV_DMA_IN_MEMORY_CLASS, 0, 0),
                     0xff, 0x2000);
    g_assert_true(nv_dma_load_from_ramin_checked(
        ramin, sizeof(ramin), 0, &queued));

    write_dma_object(ramin, 0,
                     report_dma_flags(NV_DMA_IN_MEMORY_CLASS, 0, 0),
                     0x1ff, 0x9000);
    g_assert_true(nv_dma_load_from_ramin_checked(
        ramin, sizeof(ramin), 0, &rewritten));

    g_assert_cmphex(queued.address, ==, 0x2000);
    g_assert_cmphex(queued.limit, ==, 0xff);
    g_assert_cmphex(rewritten.address, ==, 0x9000);
    g_assert_cmphex(rewritten.limit, ==, 0x1ff);
}

static void test_report_object_support(void)
{
    DMAObject dma = {
        .dma_class = NV_DMA_IN_MEMORY_CLASS,
        .dma_target = GET_MASK(NV_DMA_TARGET_NVM, NV_DMA_TARGET),
        .address = 0x1000,
        .limit = 0xff,
    };

    g_assert_true(nv_dma_report_object_supported(&dma));

    dma.dma_class = NV_DMA_TO_MEMORY_CLASS;
    g_assert_false(nv_dma_report_object_supported(&dma));
    dma.dma_class = NV_DMA_IN_MEMORY_CLASS;

    dma.dma_target = GET_MASK(NV_DMA_TARGET_NVM_TILED, NV_DMA_TARGET);
    g_assert_false(nv_dma_report_object_supported(&dma));
    dma.dma_target = GET_MASK(NV_DMA_TARGET_PCI, NV_DMA_TARGET);
    g_assert_true(nv_dma_report_object_supported(&dma));
    dma.dma_target = GET_MASK(NV_DMA_TARGET_AGP, NV_DMA_TARGET);
    g_assert_false(nv_dma_report_object_supported(&dma));
    dma.dma_target = GET_MASK(NV_DMA_TARGET_NVM, NV_DMA_TARGET);

    dma.address = 0x08000000;
    g_assert_false(nv_dma_report_object_supported(&dma));
}

static void test_report_record_bounds(void)
{
    DMAObject dma = {
        .dma_class = NV_DMA_IN_MEMORY_CLASS,
        .dma_target = GET_MASK(NV_DMA_TARGET_NVM, NV_DMA_TARGET),
        .address = 0x100,
        .limit = 15,
    };
    hwaddr address = HWADDR_MAX;

    g_assert_true(nv_dma_report_record_address(&dma, 0, 16, 0x110,
                                               &address));
    g_assert_cmphex(address, ==, 0x100);

    dma.limit = 14;
    g_assert_false(nv_dma_report_record_address(&dma, 0, 16, 0x110,
                                                &address));
    dma.limit = 15;
    g_assert_false(nv_dma_report_record_address(&dma, 1, 16, 0x111,
                                                &address));
    g_assert_false(nv_dma_report_record_address(&dma, 0, 16, 0x10f,
                                                &address));
    g_assert_false(nv_dma_report_record_address(&dma, 0, 0, 0x110,
                                                &address));

    dma.address = 0x110;
    g_assert_false(nv_dma_report_record_address(&dma, 0, 16, 0x110,
                                                &address));
}

static void test_retail_pci_report_bounds(void)
{
    /* Exact class/target/base observed at Halo's frozen main menu. */
    DMAObject dma = {
        .dma_class = NV_DMA_IN_MEMORY_CLASS,
        .dma_target = GET_MASK(NV_DMA_TARGET_PCI, NV_DMA_TARGET),
        .address = 0,
        .limit = 0x03ffffff,
    };
    hwaddr address = HWADDR_MAX;
    const hwaddr ram_size = 64 * 1024 * 1024;

    g_assert_true(nv_dma_report_record_address(
        &dma, 0x100, 16, ram_size, &address));
    g_assert_cmphex(address, ==, 0x100);
    g_assert_true(nv_dma_report_record_address(
        &dma, ram_size - 16, 16, ram_size, &address));
    g_assert_cmphex(address, ==, ram_size - 16);

    /* Accepting PCI must not relax either the DMA or the RAM end guard. */
    address = HWADDR_MAX;
    g_assert_false(nv_dma_report_record_address(
        &dma, ram_size - 15, 16, ram_size, &address));
    g_assert_cmphex(address, ==, HWADDR_MAX);
    dma.limit = 0x10e;
    g_assert_false(nv_dma_report_record_address(
        &dma, 0x100, 16, ram_size, &address));
    dma.limit = 0x03ffffff;
    g_assert_false(nv_dma_report_record_address(
        &dma, 0x100, 16, 0x10f, &address));
    dma.address = 0x08000000;
    g_assert_false(nv_dma_report_record_address(
        &dma, 0, 16, ram_size, &address));
    g_assert_cmphex(address, ==, HWADDR_MAX);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/xbox/nv2a/dma/descriptor-extent",
                    test_descriptor_extent);
    g_test_add_func("/xbox/nv2a/dma/descriptor-snapshot",
                    test_descriptor_snapshot);
    g_test_add_func("/xbox/nv2a/dma/report-object-support",
                    test_report_object_support);
    g_test_add_func("/xbox/nv2a/dma/report-record-bounds",
                    test_report_record_bounds);
    g_test_add_func("/xbox/nv2a/dma/retail-pci-report-bounds",
                    test_retail_pci_report_bounds);

    return g_test_run();
}
