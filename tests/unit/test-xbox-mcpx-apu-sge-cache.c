/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/bitmap.h"
#include "qom/object.h"
#include "exec/cpu-common.h"
#include "exec/page-vary.h"
#include "exec/ramlist.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "hw/core/cpu.h"
#include "hw/qdev-core.h"
#include "system/memory.h"
#include "system/ioport.h"
#include "system/ramblock.h"
#include "system/tcg.h"
#include "hw/xbox/mcpx/apu/vp/vp.c"

struct McpxApuDebug g_dbg, g_dbg_cache;

/* The memory engine is real; these target/CPU hooks are outside this test. */
__thread CPUState *current_cpu;
bool tcg_allowed;

Object *machine_get_container(const char *name)
{
    static Object *container;

    if (!container) {
        container = object_new(TYPE_CONTAINER);
    }
    return container;
}

bool target_big_endian(void)
{
    return false;
}

CPUState *qemu_get_cpu(int index)
{
    return NULL;
}

void tb_invalidate_phys_range(CPUState *cpu, tb_page_addr_t start,
                              tb_page_addr_t end)
{
}

void finalize_target_page_bits(void)
{
}

static uint64_t unassigned_read(void *opaque, hwaddr addr, unsigned size)
{
    return UINT64_MAX;
}

static void unassigned_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
}

const MemoryRegionOps unassigned_io_ops = {
    .read = unassigned_read,
    .write = unassigned_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static MemoryRegion ram;
static RAMBlock block;
static uint8_t *bytes;

static void init_memory(void)
{
    const size_t ram_size = 0x10000;
    Object *owner = object_new(TYPE_CONTAINER);

    bytes = g_malloc0(ram_size);
    memory_region_init(&ram, owner, "voice-samples-ram", ram_size);
    ram.ram = true;
    ram.terminates = true;
    block.host = bytes;
    block.used_length = block.max_length = ram_size;
    block.mr = &ram;
    ram.ram_block = &block;
    memory_region_add_subregion(get_system_memory(), 0, &ram);
}

static MemoryRegion sample_overlay, descriptor_overlay, partial_overlay;
static unsigned int sample_reads, descriptor_reads;

static uint64_t sample_read(void *opaque, hwaddr addr, unsigned size)
{
    sample_reads++;
    stl_le_phys(&address_space_memory, 0x2000, 0x5000);
    return 123;
}

static uint64_t descriptor_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0x6000 + 0x1000 * descriptor_reads++;
}

static uint64_t partial_read(void *opaque, hwaddr addr, unsigned size)
{
    return descriptor_reads++;
}

static void overlay_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned size)
{
}

static const MemoryRegionOps sample_ops = {
    .read = sample_read,
    .write = overlay_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
    .impl = {.min_access_size = 4, .max_access_size = 4},
};

static const MemoryRegionOps descriptor_ops = {
    .read = descriptor_read,
    .write = overlay_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
    .impl = {.min_access_size = 4, .max_access_size = 4},
};

static const MemoryRegionOps partial_ops = {
    .read = partial_read,
    .write = overlay_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 2, .max_access_size = 2},
    .impl = {.min_access_size = 2, .max_access_size = 2},
};

static void test_live_descriptor(void)
{
    g_auto(MCPXAPUSGETranslationCache) cache = {0};

    stl_le_phys(&address_space_memory, 0x2000, 0x4000);
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 12, &cache), ==, 0x400c);
    uint8_t *mapping = cache.mapping.ptr;
    g_assert_nonnull(mapping);
    stl_le_phys(&address_space_memory, 0x2000, 0x5000);
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 12, &cache), ==, 0x500c);
    g_assert_true(cache.mapping.ptr == mapping);
    mcpx_apu_sge_cache_clear(&cache);
    g_assert_null(cache.mapping.mrs.mr);
    g_assert_null(cache.mapping.fv);
}

static void test_sample_changes_descriptor(void)
{
    g_auto(MCPXAPUSGETranslationCache) cache = {0};

    stl_le_phys(&address_space_memory, 0x2000, 0x4000);
    stl_le_phys(&address_space_memory, 0x4004, 100);
    stl_le_phys(&address_space_memory, 0x5004, 200);
    memory_region_add_subregion_overlap(get_system_memory(), 0x4000,
                                        &sample_overlay, 1);
    sample_reads = 0;
    hwaddr first = get_data_ptr(0x2000, UINT_MAX, 0, &cache);
    g_assert_cmphex(first, ==, 0x4000);
    g_assert_cmpuint(ldl_le_phys(&address_space_memory, first), ==, 123);
    g_assert_cmpuint(sample_reads, ==, 1);
    g_assert_cmphex(ldl_le_phys(&address_space_memory, 0x2000), ==, 0x5000);
    hwaddr second = get_data_ptr(0x2000, UINT_MAX, 4, &cache);
    g_assert_cmpuint(ldl_le_phys(&address_space_memory, second), ==, 200);
    memory_region_del_subregion(get_system_memory(), &sample_overlay);
}

static void test_table_and_page_changes(void)
{
    g_auto(MCPXAPUSGETranslationCache) cache = {0};

    stl_le_phys(&address_space_memory, 0x2000, 0x4000);
    stl_le_phys(&address_space_memory, 0x2008, 0x6000);
    stl_le_phys(&address_space_memory, 0x3000, 0x5000);
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 8, &cache), ==, 0x4008);
    g_assert_cmphex(
        get_data_ptr(0x2000, UINT_MAX, TARGET_PAGE_SIZE + 8, &cache), ==,
        0x6008);
    g_assert_cmphex(get_data_ptr(0x3000, UINT_MAX, 8, &cache), ==, 0x5008);
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 8, &cache), ==, 0x4008);
}

static void test_descriptor_overlay(void)
{
    g_auto(MCPXAPUSGETranslationCache) cache = {0};

    stl_le_phys(&address_space_memory, 0x2000, 0x4000);
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 0, &cache), ==, 0x4000);
    g_assert_nonnull(cache.mapping.ptr);
    memory_region_add_subregion_overlap(get_system_memory(), 0x2000,
                                        &descriptor_overlay, 1);
    descriptor_reads = 0;
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 4, &cache), ==, 0x6004);
    g_assert_null(cache.mapping.mrs.mr);
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 8, &cache), ==, 0x7008);
    g_assert_cmpuint(descriptor_reads, ==, 2);
    memory_region_del_subregion(get_system_memory(), &descriptor_overlay);
    g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 12, &cache), ==, 0x400c);
    g_assert_nonnull(cache.mapping.ptr);
}

static void test_partial_descriptor_mapping(void)
{
    g_auto(MCPXAPUSGETranslationCache) cache = {0};

    stl_le_phys(&address_space_memory, 0x2000, 0x4000);
    memory_region_add_subregion_overlap(get_system_memory(), 0x2002,
                                        &partial_overlay, 1);
    for (unsigned int i = 0; i < 4; i++) {
        descriptor_reads = i;
        uint32_t expected = ldl_le_phys(&address_space_memory, 0x2000);
        unsigned int reads = descriptor_reads;
        /* Preserve the normal accessor's split/rejected access policy. */
        descriptor_reads = i;
        g_assert_cmphex(get_data_ptr(0x2000, UINT_MAX, 4, &cache), ==,
                        (hwaddr)expected + 4);
        g_assert_cmpuint(descriptor_reads, ==, reads);
        g_assert_null(cache.mapping.mrs.mr);
    }
    memory_region_del_subregion(get_system_memory(), &partial_overlay);
}

static hwaddr scoped_translation(void)
{
    g_auto(MCPXAPUSGETranslationCache) cache = {0};

    return get_data_ptr(0x2000, UINT_MAX, 0, &cache);
}

static void test_early_return_cleanup(void)
{
    stl_le_phys(&address_space_memory, 0x2000, 0x4000);
    unsigned int refs = ram.owner->ref;
    for (int i = 0; i < 64; i++) {
        g_assert_cmphex(scoped_translation(), ==, 0x4000);
        g_assert_cmpuint(ram.owner->ref, ==, refs);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    cpu_exec_init_all();
    rust_bql_mock_lock();
    init_memory();
    memory_region_init_io(&sample_overlay, OBJECT(&ram), &sample_ops, NULL,
                          "sample-write-trigger", 4);
    memory_region_init_io(&descriptor_overlay, OBJECT(&ram), &descriptor_ops,
                          NULL, "descriptor-overlay", 4);
    memory_region_init_io(&partial_overlay, OBJECT(&ram), &partial_ops, NULL,
                          "partial-descriptor-overlay", 2);
    g_test_add_func("/xbox/apu/sge/live-descriptor", test_live_descriptor);
    g_test_add_func("/xbox/apu/sge/sample-rewrites-descriptor",
                    test_sample_changes_descriptor);
    g_test_add_func("/xbox/apu/sge/table-and-page-changes",
                    test_table_and_page_changes);
    g_test_add_func("/xbox/apu/sge/descriptor-overlay",
                    test_descriptor_overlay);
    g_test_add_func("/xbox/apu/sge/partial-mapping",
                    test_partial_descriptor_mapping);
    g_test_add_func("/xbox/apu/sge/early-return-cleanup",
                    test_early_return_cleanup);
    /* The unit BQL stub cannot serialize the RCU reclaimer's transactions. */
    rcu_read_lock();
    int result = g_test_run();
    rcu_read_unlock();
    drain_call_rcu();
    return result;
}
