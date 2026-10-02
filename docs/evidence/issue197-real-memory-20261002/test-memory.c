/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "system/cpus.h"
#include "hw/boards.h"
#include "hw/xbox/mcpx/apu/vp/sample-memory.h"

static MemoryRegion table, old_bank, new_bank;
static unsigned descriptor_reads;
static bool remap_enabled;
static uint32_t table_base = 0x1000;
static bool original_reader;

static uint64_t read_table(void *opaque, hwaddr addr, unsigned size)
{
    g_assert_cmpuint(addr, ==, 0);
    g_assert_cmpuint(size, ==, 4);
    descriptor_reads++;
    if (remap_enabled && descriptor_reads == 2) {
        memory_region_transaction_begin();
        memory_region_del_subregion(get_system_memory(), &old_bank);
        memory_region_add_subregion(get_system_memory(), 0x4000, &new_bank);
        memory_region_transaction_commit();
    }
    return 0x4000;
}
static const MemoryRegionOps table_ops = {
    .read = read_table,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void prepare_memory(void)
{
    descriptor_reads = 0;
    memory_region_init_io(&table, NULL, &table_ops, NULL, "test-sge-table", 4096);
    memory_region_init_ram(&old_bank, NULL, "test-old-bank", 4096, &error_fatal);
    memory_region_init_ram(&new_bank, NULL, "test-new-bank", 4096, &error_fatal);
    uint8_t *old_data = memory_region_get_ram_ptr(&old_bank);
    uint8_t *new_data = memory_region_get_ram_ptr(&new_bank);
    stl_le_p(old_data, 0x11223344);
    stl_le_p(old_data + 4, 0xdeadbeef);
    stl_le_p(old_data + 8, 0xbad0cafe);
    stl_le_p(new_data + 4, 0x55667788);
    stl_le_p(new_data + 8, 0x99aabbcc);
    memory_region_add_subregion(get_system_memory(), 0x1000, &table);
    memory_region_add_subregion(get_system_memory(), 0x4000, &old_bank);
    remap_enabled = true;
}

static void test_mapping_replaced_between_words(void)
{
    uint32_t words[3];
    prepare_memory();
    if (original_reader) {
        uint32_t linear = 0;
        for (unsigned i = 0; i < 3; i++) {
            hwaddr base = table_base;
            hwaddr descriptor = base + (linear / TARGET_PAGE_SIZE) * 8;
            uint32_t page = ldl_le_phys(&address_space_memory, descriptor);
            words[i] = ldl_le_phys(&address_space_memory,
                                  page + linear % TARGET_PAGE_SIZE);
            linear += 4;
        }
    } else {
        mcpx_apu_read_adpcm_block(&table_base, 0, words, 3);
    }
    g_assert_cmphex(words[0], ==, 0x11223344);
    g_assert_cmphex(words[1], ==, 0x55667788);
    g_assert_cmphex(words[2], ==, 0x99aabbcc);
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--baseline")) {
        original_reader = true;
        memmove(argv + 1, argv + 2, (argc - 1) * sizeof(*argv));
        argc--;
    }
    g_test_init(&argc, &argv, NULL);
    qemu_init_cpu_loop();
    bql_lock();
    module_call_init(MODULE_INIT_QOM);
    current_machine = MACHINE(object_new("xbox-machine"));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_new_container(OBJECT(current_machine), "unattached");
    cpu_exec_init_all();
    g_test_add_func("/mcpx-apu/sample-memory/real-mapping-replaced-between-words",
                    test_mapping_replaced_between_words);
    return g_test_run();
}
