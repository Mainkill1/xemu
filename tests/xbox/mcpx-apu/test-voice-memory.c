/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "system/cpus.h"
#include "system/address-spaces.h"
#include "system/memory.h"

static unsigned maps, destroys, fallbacks, io_reads;

static int64_t tracked_init(MemoryRegionCache *cache, AddressSpace *as,
                            hwaddr address, hwaddr size, bool write)
{
    maps++;
    return address_space_cache_init(cache, as, address, size, write);
}

static void tracked_destroy(MemoryRegionCache *cache)
{
    destroys += cache->mrs.mr != NULL;
    address_space_cache_destroy(cache);
}

static uint32_t tracked_read(AddressSpace *as, hwaddr address)
{
    fallbacks++;
    return ldl_le_phys(as, address);
}

#define address_space_cache_init tracked_init
#define address_space_cache_destroy tracked_destroy
#define ldl_le_phys tracked_read
#include "hw/xbox/mcpx/apu/vp/voice-memory.h"
#undef ldl_le_phys
#undef address_space_cache_destroy
#undef address_space_cache_init

static MemoryRegion ram, replacement, io;

static uint64_t io_read(void *opaque, hwaddr address, unsigned size)
{
    io_reads++;
    return 0xa5a5a5a5;
}

static const MemoryRegionOps io_ops = {
    .read = io_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static void reset_counts(void)
{
    maps = destroys = fallbacks = io_reads = 0;
}

/* Catch replacing fresh loads with a snapshot or remapping each field. */
static void test_fresh_words(void)
{
    g_auto(MCPXAPUVoiceReadCache) cache;
    memset(&cache, 0xa5, sizeof(cache));
    cache.mapping.mrs.mr = NULL;
    reset_counts();
    for (unsigned pass = 0; pass < 3; pass++) {
        for (unsigned offset = 0; offset < 128; offset += 4) {
            uint32_t value = 0x10203040 + offset + pass;
            stl_le_phys(&address_space_memory, 0x4000 + offset, value);
            g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, offset),
                            ==, value);
        }
    }
    g_assert_cmpuint(fallbacks, ==, 0);
    g_assert_cmpuint(maps, ==, 1);
    mcpx_apu_voice_read_cache_clear(&cache);
    g_assert_cmpuint(destroys, ==, 1);
    g_assert_null(cache.mapping.mrs.mr);
}

static void test_base_change(void)
{
    g_auto(MCPXAPUVoiceReadCache) cache = { 0 };
    stl_le_phys(&address_space_memory, 0x4000, 0x11223344);
    stl_le_phys(&address_space_memory, 0x4080, 0x55667788);
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, 0), ==,
                    0x11223344);
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4080, 0), ==,
                    0x55667788);
}

static void test_mapping_replacement(void)
{
    g_auto(MCPXAPUVoiceReadCache) cache = { 0 };
    stl_le_phys(&address_space_memory, 0x4000, 0x11223344);
    stl_le_p(memory_region_get_ram_ptr(&replacement), 0x55667788);
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, 0), ==,
                    0x11223344);
    memory_region_transaction_begin();
    memory_region_del_subregion(get_system_memory(), &ram);
    memory_region_add_subregion(get_system_memory(), 0x4000, &replacement);
    memory_region_transaction_commit();
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, 0), ==,
                    0x55667788);
    mcpx_apu_voice_read_cache_clear(&cache);
    memory_region_transaction_begin();
    memory_region_del_subregion(get_system_memory(), &replacement);
    memory_region_add_subregion(get_system_memory(), 0x4000, &ram);
    memory_region_transaction_commit();
}

static void test_partial_overlay(void)
{
    g_auto(MCPXAPUVoiceReadCache) cache = { 0 };
    MemoryRegion overlay;
    stl_le_phys(&address_space_memory, 0x4000, 0x11223344);
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, 0), ==,
                    0x11223344);
    memory_region_init_io(&overlay, NULL, &io_ops, NULL, "voice-overlay", 2);
    memory_region_add_subregion_overlap(get_system_memory(), 0x4002, &overlay,
                                        1);
    io_reads = 0;
    uint32_t expected = ldl_le_phys(&address_space_memory, 0x4000);
    unsigned expected_io_reads = io_reads;
    g_assert_cmphex(expected, !=, 0x11223344);
    reset_counts();
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, 0), ==, expected);
    g_assert_cmpuint(fallbacks, ==, 1);
    g_assert_cmpuint(io_reads, ==, expected_io_reads);
    mcpx_apu_voice_read_cache_clear(&cache);
    memory_region_del_subregion(get_system_memory(), &overlay);
    memory_region_destroy(&overlay);
}

static void test_mmio(void)
{
    g_auto(MCPXAPUVoiceReadCache) cache = { 0 };
    reset_counts();
    for (unsigned i = 0; i < 8; i++) {
        g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0xc000, 0), ==,
                        0xa5a5a5a5);
    }
    g_assert_cmpuint(io_reads, ==, 8);
    g_assert_cmpuint(fallbacks, ==, 8);
}

static void *write_word(void *opaque)
{
    uint8_t *bytes = memory_region_get_ram_ptr(&ram);
    stl_le_p(bytes, 0xfeedface);
    return NULL;
}

static void test_guest_writer(void)
{
    g_auto(MCPXAPUVoiceReadCache) cache = { 0 };
    QemuThread writer;
    stl_le_phys(&address_space_memory, 0x4000, 0x11223344);
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, 0), ==,
                    0x11223344);
    qemu_thread_create(&writer, "voice-writer", write_word, NULL,
                       QEMU_THREAD_JOINABLE);
    qemu_thread_join(&writer);
    g_assert_cmphex(mcpx_apu_voice_read_word(&cache, 0x4000, 0), ==,
                    0xfeedface);
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qemu_init_cpu_loop();
    bql_lock();
    module_call_init(MODULE_INIT_QOM);
    current_machine = MACHINE(object_new("xbox-machine"));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_new_container(OBJECT(current_machine), "unattached");
    cpu_exec_init_all();
    memory_region_init_ram(&ram, NULL, "voice-ram", 4096, &error_fatal);
    memory_region_init_ram(&replacement, NULL, "voice-replacement", 4096,
                           &error_fatal);
    memory_region_init_io(&io, NULL, &io_ops, NULL, "voice-io", 4096);
    memory_region_add_subregion(get_system_memory(), 0x4000, &ram);
    memory_region_add_subregion(get_system_memory(), 0xc000, &io);
    g_test_add_func("/mcpx-apu/voice-memory/fresh-words", test_fresh_words);
    g_test_add_func("/mcpx-apu/voice-memory/base-change", test_base_change);
    g_test_add_func("/mcpx-apu/voice-memory/remap", test_mapping_replacement);
    g_test_add_func("/mcpx-apu/voice-memory/partial-overlay",
                    test_partial_overlay);
    g_test_add_func("/mcpx-apu/voice-memory/mmio", test_mmio);
    g_test_add_func("/mcpx-apu/voice-memory/guest-writer", test_guest_writer);
    return g_test_run();
}
