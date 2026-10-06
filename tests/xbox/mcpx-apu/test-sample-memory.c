/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"
#include "qapi/error.h"
#include "system/cpus.h"
#include "system/ram_addr.h"
#include "hw/boards.h"
#include "system/address-spaces.h"
#include "system/memory.h"

static unsigned payload_map_inits, payload_map_destroys;

static int64_t tracked_cache_init(MemoryRegionCache *cache, AddressSpace *as,
                                  hwaddr address, hwaddr size, bool write)
{
    if (address >= 0x4000) {
        payload_map_inits++;
    }
    return address_space_cache_init(cache, as, address, size, write);
}

static void tracked_cache_destroy(MemoryRegionCache *cache)
{
    if (cache->mrs.mr && cache->len > sizeof(uint32_t)) {
        payload_map_destroys++;
    }
    address_space_cache_destroy(cache);
}

#define address_space_cache_init tracked_cache_init
#define address_space_cache_destroy tracked_cache_destroy
#include "hw/xbox/mcpx/apu/vp/sample-memory.h"
#undef address_space_cache_init
#undef address_space_cache_destroy
#include "hw/xbox/mcpx/apu/vp/adpcm.h"

static MemoryRegion table, ram_table, old_bank, new_bank;
static MemoryRegion tail_bank, io_bank, other_table;
static unsigned descriptor_reads;
static bool remap_enabled;
static uint32_t table_base = 0x1000;
static bool original_reader;
static bool new_bank_mapped;
static bool threaded_change;
static uint32_t descriptor_page = 0x4000;
static unsigned payload_reads;
static QemuEvent remap_requested, remap_finished;
static const unsigned warmup_blocks = 4096;

typedef struct LifetimeRam {
    Object parent_obj;
    MemoryRegion ram;
    unsigned *finalized;
} LifetimeRam;

static void lifetime_ram_finalize(Object *obj)
{
    LifetimeRam *bank = (LifetimeRam *)obj;
    qatomic_inc(bank->finalized);
}

static const TypeInfo lifetime_ram_type = {
    .name = "test-mcpx-lifetime-ram",
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(LifetimeRam),
    .instance_finalize = lifetime_ram_finalize,
};

static LifetimeRam *lifetime_ram_new(unsigned *finalized, bool resizable)
{
    LifetimeRam *bank = (LifetimeRam *)object_new(lifetime_ram_type.name);
    bank->finalized = finalized;
    if (resizable) {
        memory_region_init_resizeable_ram(&bank->ram, OBJECT(bank),
                                          "test-resizable-payload", 8192, 8192,
                                          NULL, &error_fatal);
    } else {
        memory_region_init_ram_nomigrate(
            &bank->ram, OBJECT(bank), "test-owned-payload", 4096, &error_fatal);
    }
    return bank;
}

static unsigned resize_payload_reads;

static uint64_t read_resize_payload(void *opaque, hwaddr addr, unsigned size)
{
    g_assert_cmpuint(size, ==, 4);
    g_assert_cmphex(addr, ==, 4096 + resize_payload_reads * 4);
    return 0x55660000U + resize_payload_reads++;
}

static const MemoryRegionOps resize_payload_ops = {
    .read = read_resize_payload,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static MemoryRegion resize_fallback;

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
        g_auto(MCPXAPUSampleReadCache) cache QEMU_UNINITIALIZED;

        /* Catch reads of unused fields before QEMU initializes a mapping. */
        memset(&cache, 0xa5, sizeof(cache));
        cache.sge.mapping.mrs.mr = NULL;
        cache.payload.mrs.mr = NULL;
        mcpx_apu_read_adpcm_block(&cache, &table_base, linear, words,
                                  word_count);
    }
}

static uint32_t fixture_word(unsigned index)
{
    return 0x10203040U ^ (index * 0x9e3779b9U);
}

static uint64_t read_table(void *opaque, hwaddr addr, unsigned size)
{
    g_assert_true(addr == 0 || addr == 8);
    g_assert_cmpuint(size, ==, 4);
    descriptor_reads++;
    if (threaded_change && descriptor_reads == 2) {
        qemu_event_set(&remap_requested);
        qemu_event_wait(&remap_finished);
    }
    if (remap_enabled && descriptor_reads == 2) {
        memory_region_transaction_begin();
        memory_region_del_subregion(get_system_memory(), &old_bank);
        memory_region_add_subregion(get_system_memory(), 0x4000, &new_bank);
        memory_region_transaction_commit();
        new_bank_mapped = true;
    }
    return addr == 0 ? descriptor_page : 0x8000;
}
static const MemoryRegionOps table_ops = {
    .read = read_table,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static uint64_t read_payload(void *opaque, hwaddr addr, unsigned size)
{
    g_assert_cmpuint(size, ==, 4);
    g_assert_cmpuint(addr, ==, payload_reads * 4);
    return fixture_word(payload_reads++);
}

static const MemoryRegionOps payload_ops = {
    .read = read_payload,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void initialize_memory(void)
{
    memory_region_init_io(&table, NULL, &table_ops, NULL, "test-sge-table",
                          4096);
    /*
     * This synthetic descriptor is synchronized by events. Its reader must
     * not take BQL while the main thread owns BQL to change the RAM map.
     */
    memory_region_enable_lockless_io(&table);
    memory_region_init_ram(&ram_table, NULL, "test-ram-table", 4096,
                           &error_fatal);
    memory_region_init_ram(&old_bank, NULL, "test-old-bank", 4096,
                           &error_fatal);
    memory_region_init_ram(&new_bank, NULL, "test-new-bank", 4096,
                           &error_fatal);
    memory_region_init_ram(&other_table, NULL, "test-other-table", 4096,
                           &error_fatal);
    stl_le_p(memory_region_get_ram_ptr(&other_table), 0x8000);
    memory_region_add_subregion(get_system_memory(), 0x2000, &other_table);
    memory_region_init_ram(&tail_bank, NULL, "test-tail-bank", 4096,
                           &error_fatal);
    memory_region_init_io(&io_bank, NULL, &payload_ops, NULL,
                          "test-payload-mmio", 4096);
    stl_le_p(memory_region_get_ram_ptr(&ram_table), 0x4000);
    memory_region_add_subregion(get_system_memory(), 0x1000, &table);
    memory_region_add_subregion(get_system_memory(), 0x4000, &old_bank);
    memory_region_add_subregion(get_system_memory(), 0x8000, &tail_bank);
    memory_region_add_subregion(get_system_memory(), 0xc000, &io_bank);
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
    threaded_change = false;
    descriptor_page = 0x4000;
    table_base = 0x1000;
    payload_reads = 0;
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

/*
 * Valid IMA blocks whose known decoded samples are independent ramps.
 * Nibble 1 at step index 0 adds exactly one and keeps the index clamped at 0;
 * nibble 9 subtracts one. Stereo payload is interleaved in four-byte chunks.
 */
static void test_decoded_ramp(gconstpointer opaque)
{
    unsigned variant = GPOINTER_TO_UINT(opaque);
    unsigned channels = variant < 2 ? 1 : 2;
    bool crossing = variant & 1;
    unsigned bytes = 36 * channels;
    uint32_t linear = crossing ? TARGET_PAGE_SIZE - 8 * channels : 128;
    uint8_t encoded[72] = { 0 };
    uint32_t words[18];
    int16_t decoded[65 * 2] = { 0 };
    reset_memory();
    for (unsigned channel = 0; channel < channels; channel++) {
        stw_le_p(encoded + 4 * channel, channel ? -1000 : 1000);
    }
    for (unsigned chunk = 0; chunk < 8; chunk++) {
        for (unsigned channel = 0; channel < channels; channel++) {
            memset(encoded + 4 * channels + (chunk * channels + channel) * 4,
                   channel ? 0x99 : 0x11, 4);
        }
    }
    for (unsigned i = 0; i < bytes; i++) {
        uint32_t address = linear + i;
        MemoryRegion *bank =
            address < TARGET_PAGE_SIZE ? &old_bank : &tail_bank;
        uint8_t *data = memory_region_get_ram_ptr(bank);
        data[address % TARGET_PAGE_SIZE] = encoded[i];
    }
    read_block(linear, words, bytes / 4);
    g_assert_cmpmem(words, bytes, encoded, bytes);
    g_assert_cmpuint(descriptor_reads, ==, bytes / 4);
    MCPXAPUADPCMDecodeTable table;
    mcpx_apu_adpcm_decode_table_init(&table);
    g_assert_cmpint(adpcm_decode_block(&table, decoded, (uint8_t *)words,
                                      bytes, channels), ==, 65);
    for (unsigned sample = 0; sample < 65; sample++) {
        for (unsigned channel = 0; channel < channels; channel++) {
            int expected = channel ? -1000 - (int)sample : 1000 + sample;
            g_assert_cmpint(decoded[sample * channels + channel], ==, expected);
        }
    }
}

static void test_mmio_payload(void)
{
    uint32_t words[9];
    reset_memory();
    descriptor_page = 0xc000;
    read_block(0, words, G_N_ELEMENTS(words));
    g_assert_cmpuint(payload_reads, ==, G_N_ELEMENTS(words));
    g_assert_cmpuint(descriptor_reads, ==, G_N_ELEMENTS(words));
    for (unsigned i = 0; i < G_N_ELEMENTS(words); i++) {
        g_assert_cmphex(words[i], ==, fixture_word(i));
    }
}

typedef struct LifetimeRead {
    uint32_t words[9];
    unsigned word_count;
} LifetimeRead;

static void *lifetime_reader(void *opaque)
{
    LifetimeRead *read = opaque;
    rcu_register_thread();
    read_block(0, read->words, read->word_count);
    /*
     * The reader can release the last cached view reference. Drain on the
     * thread that queued it; the main thread releases BQL while joining.
     */
    drain_call_rcu();
    rcu_unregister_thread();
    return NULL;
}

/*
 * Dropping the view guard would reuse stale RAM after either an owned region
 * retires or a shrink exposes MMIO. Events stop the reader at descriptor 2;
 * every payload expectation is independent of the cache implementation.
 */
static void test_owned_memory_lifetime(gconstpointer opaque)
{
    bool resizing = GPOINTER_TO_UINT(opaque);
    unsigned finalized = 0;
    qemu_event_init(&remap_requested, false);
    qemu_event_init(&remap_finished, false);
    for (unsigned generation = 0; generation < 64; generation++) {
        reset_memory();
        resize_payload_reads = 0;
        LifetimeRam *old = lifetime_ram_new(&finalized, resizing);
        LifetimeRam *replacement =
            resizing ? NULL : lifetime_ram_new(&finalized, false);
        uint8_t *data = memory_region_get_ram_ptr(&old->ram);
        unsigned offset = resizing ? 4092 : 0;
        stl_le_p(data + offset, 0x11223344);
        for (unsigned i = 1; i < 9; i++) {
            stl_le_p(data + offset + 4 * i, 0xdeadbeef);
        }
        if (replacement) {
            uint8_t *next = memory_region_get_ram_ptr(&replacement->ram);
            stl_le_p(next + 4, 0x55667788);
            stl_le_p(next + 8, 0x99aabbcc);
        }
        memory_region_add_subregion_overlap(get_system_memory(), 0x40000,
                                            &old->ram, 1);
        descriptor_page = 0x40000 + offset;
        threaded_change = true;
        qemu_event_reset(&remap_requested);
        qemu_event_reset(&remap_finished);
        LifetimeRead read = { .word_count = resizing ? 9 : 3 };
        QemuThread reader;
        qemu_thread_create(&reader, "lifetime-reader", lifetime_reader, &read,
                           QEMU_THREAD_JOINABLE);
        qemu_event_wait(&remap_requested);
        if (resizing) {
            g_assert_cmpint(
                qemu_ram_resize(old->ram.ram_block, 4096, &error_fatal), ==, 0);
        } else {
            memory_region_transaction_begin();
            memory_region_del_subregion(get_system_memory(), &old->ram);
            memory_region_add_subregion_overlap(get_system_memory(), 0x40000,
                                                &replacement->ram, 1);
            memory_region_transaction_commit();
            /*
             * Release the last fixture reference. The live read must keep
             * its old backing alive until it stops using that mapping.
             */
            object_unref(OBJECT(old));
            old = NULL;
        }
        g_assert_cmpuint(qatomic_read(&finalized), ==,
                         generation * (resizing ? 1 : 2));
        qemu_event_set(&remap_finished);
        bql_unlock();
        qemu_thread_join(&reader);
        bql_lock();
        g_assert_cmphex(read.words[0], ==, 0x11223344);
        if (resizing) {
            for (unsigned i = 1; i < 9; i++) {
                g_assert_cmphex(read.words[i], ==, 0x55660000U + i - 1);
            }
            g_assert_cmpuint(resize_payload_reads, ==, 8);
        } else {
            g_assert_cmphex(read.words[1], ==, 0x55667788);
            g_assert_cmphex(read.words[2], ==, 0x99aabbcc);
        }
        g_assert_cmpuint(descriptor_reads, ==, read.word_count);
        LifetimeRam *remaining = resizing ? old : replacement;
        memory_region_del_subregion(get_system_memory(), &remaining->ram);
        object_unref(OBJECT(remaining));
        /*
         * The retired reader view was drained by its own thread. Drain the
         * final unmapping here; a cache leak prevents owner finalization.
         */
        drain_call_rcu();
        g_assert_cmpuint(qatomic_read(&finalized), ==,
                         (generation + 1) * (resizing ? 1 : 2));
    }
    threaded_change = false;
    qemu_event_destroy(&remap_finished);
    qemu_event_destroy(&remap_requested);
}

static void *remap_reader(void *opaque)
{
    rcu_register_thread();
    read_block(0, opaque, 3);
    rcu_unregister_thread();
    return NULL;
}

enum ReaderChange {
    RAM_REMAP,
    TABLE_BASE_CHANGE,
    DESCRIPTOR_CHANGE,
};

static void test_threaded_observation_change(gconstpointer opaque)
{
    enum ReaderChange change = GPOINTER_TO_UINT(opaque);
    QemuThread reader;
    unsigned remaps = 256;
    qemu_event_init(&remap_requested, false);
    qemu_event_init(&remap_finished, false);
    for (unsigned generation = 1; generation <= remaps; generation++) {
        uint32_t words[3];
        reset_memory();
        uint8_t *old_data = memory_region_get_ram_ptr(&old_bank);
        uint8_t *new_data = memory_region_get_ram_ptr(&new_bank);
        stl_le_p(old_data, generation);
        stl_le_p(old_data + 4, change == TABLE_BASE_CHANGE ?
                                   generation + 0x10000 :
                                   0xdeadbeef);
        stl_le_p(old_data + 8, 0xbad0cafe);
        stl_le_p(new_data + 4, generation + 0x10000);
        stl_le_p(new_data + 8, generation + 0x20000);
        uint8_t *tail_data = memory_region_get_ram_ptr(&tail_bank);
        /*
         * Distinguish the already-selected old word 2 from a premature
         * retranslation through the newly published table base.
         */
        stl_le_p(tail_data + 4, change == TABLE_BASE_CHANGE ?
                                    0xfeedbabe :
                                    generation + 0x10000);
        stl_le_p(tail_data + 8, generation + 0x20000);
        threaded_change = true;
        qemu_event_reset(&remap_requested);
        qemu_event_reset(&remap_finished);
        qemu_thread_create(&reader, "sample-reader", remap_reader, words,
                           QEMU_THREAD_JOINABLE);
        qemu_event_wait(&remap_requested);
        /*
         * The descriptor callback holds the reader here. BQL owns the map
         * update; events establish ordering without sleep or scheduling luck.
         */
        switch (change) {
        case RAM_REMAP:
            memory_region_transaction_begin();
            memory_region_del_subregion(get_system_memory(), &old_bank);
            memory_region_add_subregion(get_system_memory(), 0x4000, &new_bank);
            memory_region_transaction_commit();
            new_bank_mapped = true;
            break;
        case TABLE_BASE_CHANGE:
            /*
             * Word 2 already chose the old descriptor address. Word 3 must
             * reload the table base and use the other table in actual RAM.
             */
            table_base = 0x2000;
            break;
        case DESCRIPTOR_CHANGE:
            /* The blocked MMIO descriptor read returns this changed page. */
            descriptor_page = 0x8000;
            break;
        }
        qemu_event_set(&remap_finished);
        qemu_thread_join(&reader);
        g_assert_cmphex(words[0], ==, generation);
        g_assert_cmphex(words[1], ==, generation + 0x10000);
        g_assert_cmphex(words[2], ==, generation + 0x20000);
        g_assert_cmpuint(descriptor_reads, ==,
                         change == TABLE_BASE_CHANGE ? 2 : 3);
    }
    qemu_event_destroy(&remap_finished);
    qemu_event_destroy(&remap_requested);
    threaded_change = false;
}

/*
 * Both variants use this same executable, RAM fixture and fixed work.
 * The output measures encoded-word reads, not ADPCM decode or game FPS.
 */
static void test_shared_descriptor_mapping(void)
{
    g_auto(MCPXAPUSampleReadCache) cache = { 0 };
    uint32_t words[9];
    reset_memory();
    memory_region_del_subregion(get_system_memory(), &table);
    memory_region_add_subregion(get_system_memory(), 0x1000, &ram_table);
    stl_le_p(memory_region_get_ram_ptr(&ram_table), 0x4000);
    mcpx_apu_read_adpcm_block(&cache, &table_base, 0, words, 9);
    g_assert_nonnull(cache.sge.mapping.ptr);
    g_assert_cmphex(cache.sge.descriptor, ==, 0x1000);
    g_assert_cmphex(words[0], ==, fixture_word(0));
    stl_le_p(memory_region_get_ram_ptr(&ram_table), 0x8000);
    for (unsigned i = 0; i < 9; i++) {
        stl_le_p(memory_region_get_ram_ptr(&tail_bank) + i * 4,
                 fixture_word(i + 100));
    }
    mcpx_apu_read_adpcm_block(&cache, &table_base, 0, words, 9);
    for (unsigned i = 0; i < 9; i++) {
        g_assert_cmphex(words[i], ==, fixture_word(i + 100));
    }
    mcpx_apu_sample_cache_clear(&cache);
    memory_region_del_subregion(get_system_memory(), &ram_table);
    memory_region_add_subregion(get_system_memory(), 0x1000, &table);
}

static void test_payload_reused_across_blocks(void)
{
    g_auto(MCPXAPUSampleReadCache) cache = { 0 };
    uint32_t words[9];
    reset_memory();
    payload_map_inits = payload_map_destroys = 0;
    mcpx_apu_read_adpcm_block(&cache, &table_base, 0, words, 9);
    g_assert_nonnull(cache.payload.ptr);
    g_assert_cmpuint(cache.payload.len, ==, TARGET_PAGE_SIZE);
    void *original = cache.payload.ptr;
    for (unsigned block = 1; block < 32; block++) {
        mcpx_apu_read_adpcm_block(&cache, &table_base, block * 36, words, 9);
        g_assert_true(cache.payload.ptr == original);
        for (unsigned i = 0; i < 9; i++) {
            g_assert_cmphex(words[i], ==, fixture_word(block * 9 + i));
        }
    }
    g_assert_cmpuint(descriptor_reads, ==, 32 * 9);
    g_assert_cmpuint(payload_map_inits, ==, 1);
    g_assert_cmpuint(payload_map_destroys, ==, 0);
    mcpx_apu_sample_cache_clear(&cache);
    g_assert_cmpuint(payload_map_destroys, ==, 1);
}

static void test_payload_address_wrap(void)
{
    g_auto(MCPXAPUSampleReadCache) cache = { 0 };
    reset_memory();
    descriptor_page = UINT32_MAX - 3;
    g_assert_cmphex(mcpx_apu_sample_word_address(&cache, table_base, 8), ==, 4);
    g_assert_cmpuint(descriptor_reads, ==, 1);
}

/* Same descriptor cache on both sides: isolate payload mapping costs. */
static unsigned partial_io_reads;

static uint64_t read_partial_payload(void *opaque, hwaddr addr, unsigned size)
{
    g_assert_cmphex(addr, ==, 0);
    g_assert_cmpuint(size, ==, 2);
    partial_io_reads++;
    return 0xaabb;
}

static const MemoryRegionOps partial_payload_ops = {
    .read = read_partial_payload,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void test_partial_word_mapping(void)
{
    g_auto(MCPXAPUSampleReadCache) cache = { 0 };
    MemoryRegion overlay;
    uint32_t words[2];

    reset_memory();
    stl_le_p(memory_region_get_ram_ptr(&old_bank), 0xdead1122);
    memory_region_init_io(&overlay, NULL, &partial_payload_ops, NULL,
                          "test-partial-payload", 2);
    memory_region_add_subregion_overlap(get_system_memory(), 0x4002, &overlay,
                                        1);
    partial_io_reads = 0;
    uint32_t expected = ldl_le_phys(&address_space_memory, 0x4000);
    unsigned expected_io_reads = partial_io_reads;
    /* A sub-word overlay must not turn into a direct stale RAM-word load. */
    g_assert_cmphex(expected, !=, 0xdead1122);
    partial_io_reads = 0;
    mcpx_apu_read_adpcm_block(&cache, &table_base, 0, words, 2);
    g_assert_cmphex(words[0], ==, expected);
    g_assert_cmphex(words[1], ==, fixture_word(1));
    g_assert_cmpuint(partial_io_reads, ==, expected_io_reads);
    mcpx_apu_sample_cache_clear(&cache);
    memory_region_del_subregion(get_system_memory(), &overlay);
    memory_region_destroy(&overlay);
}

static void test_unmapped_payload(void)
{
    g_auto(MCPXAPUSampleReadCache) cache = { 0 };
    uint32_t words[2], expected[2];

    reset_memory();
    descriptor_page = 0x200000;
    for (unsigned i = 0; i < 2; i++) {
        expected[i] =
            ldl_le_phys(&address_space_memory, descriptor_page + i * 4);
    }
    mcpx_apu_read_adpcm_block(&cache, &table_base, 0, words, 2);
    g_assert_cmpmem(words, sizeof(words), expected, sizeof(expected));
    g_assert_null(cache.payload.ptr);
    g_assert_cmpuint(descriptor_reads, ==, 2);
}

static int run_reader_benchmark(const char *mode, unsigned count,
                                unsigned blocks)
{
    g_auto(MCPXAPUSampleReadCache) cache = { 0 };
    uint32_t words[32];
    uint64_t expected_per_offset[32] = { 0 };
    uint64_t checksum = 0, expected = 0;
    bool generic = !strcmp(mode, "generic");
    bool cold = !strcmp(mode, "cold");

    reset_memory();
    memory_region_transaction_begin();
    memory_region_del_subregion(get_system_memory(), &table);
    memory_region_add_subregion(get_system_memory(), 0x1000, &ram_table);
    memory_region_transaction_commit();
    for (unsigned offset = 0; offset < 32; offset++) {
        for (unsigned i = 0; i < count; i++) {
            expected_per_offset[offset] += fixture_word(offset * 32 + i);
        }
    }
    int64_t begin = g_get_monotonic_time();
    for (unsigned block = 0; block < blocks; block++) {
        uint32_t linear = (block % 32) * 128;
        if (generic) {
            for (unsigned i = 0; i < count; i++) {
                hwaddr physical = mcpx_apu_sample_word_address(
                    &cache, table_base, linear + i * 4);
                words[i] = ldl_le_phys(&address_space_memory, physical);
            }
        } else {
            mcpx_apu_read_adpcm_block(&cache, &table_base, linear, words,
                                      count);
            if (cold && cache.payload.mrs.mr) {
                address_space_cache_destroy(&cache.payload);
            }
        }
        for (unsigned i = 0; i < count; i++) {
            checksum += words[i];
        }
    }
    int64_t elapsed = g_get_monotonic_time() - begin;
    for (unsigned block = 0; block < blocks; block++) {
        expected += expected_per_offset[block % 32];
    }
    g_assert_cmpuint(checksum, ==, expected);
    printf("{\"mode\":\"%s\",\"blocks\":%u,\"wordsPerBlock\":%u,"
           "\"elapsedUs\":%" PRId64 ",\"checksum\":%" PRIu64 ","
           "\"expectedChecksum\":%" PRIu64 "}\n",
           mode, blocks, count, elapsed, checksum, expected);
    return 0;
}

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
    bool reader_benchmark = argc > 1 && !strcmp(argv[1], "--reader-benchmark");
    bool benchmark = argc > 1 && !strcmp(argv[1], "--benchmark");
    unsigned words = 0, blocks = 0;
    if (reader_benchmark) {
        uint64_t parsed_words, parsed_blocks;
        if (argc != 5 ||
            (strcmp(argv[2], "generic") && strcmp(argv[2], "cold") &&
             strcmp(argv[2], "reused")) ||
            qemu_strtou64(argv[3], NULL, 10, &parsed_words) ||
            qemu_strtou64(argv[4], NULL, 10, &parsed_blocks) || !parsed_words ||
            parsed_words > 32 || !parsed_blocks || parsed_blocks > UINT_MAX) {
            fprintf(stderr,
                    "Expected generic|cold|reused, 1..32 words, blocks\n");
            return 2;
        }
        words = parsed_words;
        blocks = parsed_blocks;
    } else if (benchmark) {
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
    type_register_static(&lifetime_ram_type);
    current_machine = MACHINE(object_new("xbox-machine"));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_new_container(OBJECT(current_machine), "unattached");
    cpu_exec_init_all();
    initialize_memory();
    if (reader_benchmark) {
        return run_reader_benchmark(argv[2], words, blocks);
    }
    if (benchmark) {
        return run_benchmark(words, blocks);
    }
    memory_region_init_io(&resize_fallback, NULL, &resize_payload_ops, NULL,
                          "test-resize-fallback", 8192);
    memory_region_add_subregion_overlap(get_system_memory(), 0x40000,
                                        &resize_fallback, 0);
    g_test_add_func(
        "/mcpx-apu/sample-memory/real-mapping-replaced-between-words",
        test_mapping_replaced_between_words);
    g_test_add_data_func("/mcpx-apu/sample-memory/real-ram-mono",
                         GUINT_TO_POINTER(9), test_stable_block);
    g_test_add_data_func("/mcpx-apu/sample-memory/real-ram-stereo",
                         GUINT_TO_POINTER(18), test_stable_block);
    g_test_add_data_func("/mcpx-apu/sample-memory/decoded-mono-ram",
                         GUINT_TO_POINTER(0), test_decoded_ramp);
    g_test_add_data_func("/mcpx-apu/sample-memory/decoded-mono-sge-crossing",
                         GUINT_TO_POINTER(1), test_decoded_ramp);
    g_test_add_data_func("/mcpx-apu/sample-memory/decoded-stereo-ram",
                         GUINT_TO_POINTER(2), test_decoded_ramp);
    g_test_add_data_func("/mcpx-apu/sample-memory/decoded-stereo-sge-crossing",
                         GUINT_TO_POINTER(3), test_decoded_ramp);
    g_test_add_func("/mcpx-apu/sample-memory/mmio-payload", test_mmio_payload);
    g_test_add_data_func("/mcpx-apu/sample-memory/threaded-mapping-replaced",
                         GUINT_TO_POINTER(RAM_REMAP),
                         test_threaded_observation_change);
    g_test_add_data_func("/mcpx-apu/sample-memory/threaded-table-base-change",
                         GUINT_TO_POINTER(TABLE_BASE_CHANGE),
                         test_threaded_observation_change);
    g_test_add_data_func("/mcpx-apu/sample-memory/threaded-descriptor-change",
                         GUINT_TO_POINTER(DESCRIPTOR_CHANGE),
                         test_threaded_observation_change);
    g_test_add_data_func("/mcpx-apu/sample-memory/owned-ram-retirement",
                         GUINT_TO_POINTER(0), test_owned_memory_lifetime);
    g_test_add_data_func("/mcpx-apu/sample-memory/owned-ram-resize",
                         GUINT_TO_POINTER(1), test_owned_memory_lifetime);
    g_test_add_func("/mcpx-apu/sample-memory/shared-descriptor",
                    test_shared_descriptor_mapping);
    g_test_add_func("/mcpx-apu/sample-memory/callback-page-reuse",
                    test_payload_reused_across_blocks);
    g_test_add_func("/mcpx-apu/sample-memory/32-bit-payload-address",
                    test_payload_address_wrap);
    g_test_add_func("/mcpx-apu/sample-memory/partial-word-mapping",
                    test_partial_word_mapping);
    g_test_add_func("/mcpx-apu/sample-memory/unmapped-payload",
                    test_unmapped_payload);
    return g_test_run();
}
