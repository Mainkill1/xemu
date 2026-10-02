/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "system/cpus.h"
#include "hw/boards.h"
#include "hw/xbox/mcpx/apu/vp/sample-memory.h"

static MemoryRegion table, ram_table, old_bank, new_bank;
static unsigned descriptor_reads;
static bool remap_enabled;
static uint32_t table_base = 0x1000;
static bool original_reader;
static bool new_bank_mapped;
static const unsigned warmup_blocks = 4096;

/* Original non-streaming ADPCM loads, retained as a component control. */
static void read_original_block(uint32_t linear, uint32_t *words,
                                unsigned word_count)
{
    for (unsigned i = 0; i < word_count; i++) {
        hwaddr base = table_base;
        hwaddr descriptor = base + (linear / TARGET_PAGE_SIZE) * 8;
        uint32_t page = ldl_le_phys(&address_space_memory, descriptor);
        words[i] = ldl_le_phys(&address_space_memory,
                               page + linear % TARGET_PAGE_SIZE);
        linear += 4;
    }
}

static void read_block(uint32_t linear, uint32_t *words, unsigned word_count)
{
    if (original_reader) {
        read_original_block(linear, words, word_count);
    } else {
        mcpx_apu_read_adpcm_block(&table_base, linear, words, word_count);
    }
}

static uint32_t fixture_word(unsigned index)
{
    return 0x10203040U ^ (index * 0x9e3779b9U);
}

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
        new_bank_mapped = true;
    }
    return 0x4000;
}
static const MemoryRegionOps table_ops = {
    .read = read_table,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void initialize_memory(void)
{
    memory_region_init_io(&table, NULL, &table_ops, NULL, "test-sge-table",
                          4096);
    memory_region_init_ram(&ram_table, NULL, "test-ram-table", 4096,
                           &error_fatal);
    memory_region_init_ram(&old_bank, NULL, "test-old-bank", 4096,
                           &error_fatal);
    memory_region_init_ram(&new_bank, NULL, "test-new-bank", 4096,
                           &error_fatal);
    stl_le_p(memory_region_get_ram_ptr(&ram_table), 0x4000);
    memory_region_add_subregion(get_system_memory(), 0x1000, &table);
    memory_region_add_subregion(get_system_memory(), 0x4000, &old_bank);
}

static void reset_memory(void)
{
    if (new_bank_mapped) {
        memory_region_transaction_begin();
        memory_region_del_subregion(get_system_memory(), &new_bank);
        memory_region_add_subregion(get_system_memory(), 0x4000, &old_bank);
        memory_region_transaction_commit();
        new_bank_mapped = false;
    }
    descriptor_reads = 0;
    remap_enabled = false;
    uint8_t *data = memory_region_get_ram_ptr(&old_bank);
    for (unsigned i = 0; i < 4096 / sizeof(uint32_t); i++) {
        stl_le_p(data + i * sizeof(uint32_t), fixture_word(i));
    }
}

static void test_stable_block(gconstpointer opaque)
{
    unsigned word_count = GPOINTER_TO_UINT(opaque);
    uint32_t words[18];
    reset_memory();
    read_block(128, words, word_count);
    for (unsigned i = 0; i < word_count; i++) {
        g_assert_cmphex(words[i], ==, fixture_word(128 / 4 + i));
    }
    g_assert_cmpuint(descriptor_reads, ==, word_count);
}

static void test_mapping_replaced_between_words(void)
{
    uint32_t words[3];
    reset_memory();
    uint8_t *old_data = memory_region_get_ram_ptr(&old_bank);
    uint8_t *new_data = memory_region_get_ram_ptr(&new_bank);
    stl_le_p(old_data, 0x11223344);
    stl_le_p(old_data + 4, 0xdeadbeef);
    stl_le_p(old_data + 8, 0xbad0cafe);
    stl_le_p(new_data + 4, 0x55667788);
    stl_le_p(new_data + 8, 0x99aabbcc);
    remap_enabled = true;
    read_block(0, words, G_N_ELEMENTS(words));
    g_assert_cmphex(words[0], ==, 0x11223344);
    g_assert_cmphex(words[1], ==, 0x55667788);
    g_assert_cmphex(words[2], ==, 0x99aabbcc);
}

/* Both variants use this same executable, RAM fixture and fixed work.
 * The output measures encoded-word reads, not ADPCM decode or game FPS. */
static int run_benchmark(unsigned word_count, unsigned blocks)
{
    uint32_t words[18];
    uint64_t expected_per_offset[32] = { 0 };
    uint64_t checksum = 0, expected = 0;
    reset_memory();
    memory_region_transaction_begin();
    memory_region_del_subregion(get_system_memory(), &table);
    memory_region_add_subregion(get_system_memory(), 0x1000, &ram_table);
    memory_region_transaction_commit();
    for (unsigned offset = 0; offset < 32; offset++) {
        for (unsigned i = 0; i < word_count; i++) {
            expected_per_offset[offset] += fixture_word(offset * 32 + i);
        }
    }
    for (unsigned block = 0; block < warmup_blocks; block++) {
        read_block((block % 32) * 128, words, word_count);
        for (unsigned i = 0; i < word_count; i++) {
            g_assert_cmphex(words[i], ==, fixture_word((block % 32) * 32 + i));
        }
    }
    int64_t begin = g_get_monotonic_time();
    for (unsigned block = 0; block < blocks; block++) {
        read_block((block % 32) * 128, words, word_count);
        for (unsigned i = 0; i < word_count; i++) {
            checksum += words[i];
        }
    }
    int64_t elapsed = g_get_monotonic_time() - begin;
    for (unsigned block = 0; block < blocks; block++) {
        expected += expected_per_offset[block % 32];
    }
    g_assert_cmpuint(checksum, ==, expected);
    printf("{\"mode\":\"%s\",\"blocks\":%u,\"wordsPerBlock\":%u,"
           "\"warmupBlocks\":%u,\"elapsedUs\":%" PRId64 ","
           "\"checksum\":%" PRIu64 ",\"expectedChecksum\":%" PRIu64 "}\n",
           original_reader ? "baseline" : "candidate", blocks, word_count,
           warmup_blocks, elapsed, checksum, expected);
    return 0;
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    bool benchmark = argc > 1 && !strcmp(argv[1], "--benchmark");
    unsigned words = 0, blocks = 0;
    if (benchmark) {
        if (argc != 5 ||
            (strcmp(argv[2], "baseline") && strcmp(argv[2], "candidate"))) {
            fprintf(stderr,
                    "Usage: %s --benchmark baseline|candidate 9|18 blocks\n",
                    argv[0]);
            return 2;
        }
        uint64_t parsed_words, parsed_blocks;
        if (qemu_strtou64(argv[3], NULL, 10, &parsed_words) ||
            qemu_strtou64(argv[4], NULL, 10, &parsed_blocks) ||
            (parsed_words != 9 && parsed_words != 18) || !parsed_blocks ||
            parsed_blocks > UINT_MAX) {
            fprintf(stderr, "Expected 9 or 18 words and 1..UINT_MAX blocks\n");
            return 2;
        }
        original_reader = !strcmp(argv[2], "baseline");
        words = parsed_words;
        blocks = parsed_blocks;
    } else {
        if (argc > 1 && !strcmp(argv[1], "--baseline")) {
            original_reader = true;
            memmove(argv + 1, argv + 2, (argc - 1) * sizeof(*argv));
            argc--;
        }
        g_test_init(&argc, &argv, NULL);
    }
    qemu_init_cpu_loop();
    bql_lock();
    module_call_init(MODULE_INIT_QOM);
    current_machine = MACHINE(object_new("xbox-machine"));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_new_container(OBJECT(current_machine), "unattached");
    cpu_exec_init_all();
    initialize_memory();
    if (benchmark) {
        return run_benchmark(words, blocks);
    }
    g_test_add_func(
        "/mcpx-apu/sample-memory/real-mapping-replaced-between-words",
        test_mapping_replaced_between_words);
    g_test_add_data_func("/mcpx-apu/sample-memory/real-ram-mono",
                         GUINT_TO_POINTER(9), test_stable_block);
    g_test_add_data_func("/mcpx-apu/sample-memory/real-ram-stereo",
                         GUINT_TO_POINTER(18), test_stable_block);
    return g_test_run();
}
