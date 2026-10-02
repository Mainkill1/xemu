/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"
#include "qapi/error.h"
#include "exec/ramlist.h"
#include "hw/boards.h"
#include "system/cpus.h"
#include "system/address-spaces.h"
#include "system/ram_addr.h"
#include "system/ramblock.h"
#include "hw/xbox/mcpx/apu/apu_regs.h"
#include "hw/xbox/mcpx/apu/vp/voice-store.h"

static const hwaddr word_address = 0x100000;
static const unsigned dirty_clients[] = {
    DIRTY_MEMORY_NV2A,
    DIRTY_MEMORY_NV2A_TEX,
    DIRTY_MEMORY_NV2A_SURFACE,
};

typedef struct OwnedRam {
    Object parent_obj;
    MemoryRegion ram;
    unsigned *finalized;
    bool mapped;
} OwnedRam;

static void owned_ram_finalize(Object *obj)
{
    OwnedRam *bank = (OwnedRam *)obj;
    qatomic_inc(bank->finalized);
}

static const TypeInfo owned_ram_type = {
    .name = "test-voice-store-owned-ram",
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(OwnedRam),
    .instance_finalize = owned_ram_finalize,
};

static OwnedRam *new_ram(unsigned *finalized, bool resizable)
{
    OwnedRam *bank = (OwnedRam *)object_new(owned_ram_type.name);
    bank->finalized = finalized;
    if (resizable) {
        memory_region_init_resizeable_ram(
            &bank->ram, OBJECT(bank), "test-voice-store-resizable", 0x200000,
            0x200000, NULL, &error_fatal);
    } else {
        memory_region_init_ram_nomigrate(&bank->ram, OBJECT(bank),
                                         "test-voice-store-owned", 0x200000,
                                         &error_fatal);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(dirty_clients); i++) {
        memory_region_set_log(&bank->ram, true, dirty_clients[i]);
    }
    return bank;
}

static void map_ram(OwnedRam *bank, int priority)
{
    memory_region_add_subregion_overlap(get_system_memory(), 0, &bank->ram,
                                        priority);
    bank->mapped = true;
}

static void retire_ram(OwnedRam *bank)
{
    if (bank->mapped) {
        memory_region_del_subregion(get_system_memory(), &bank->ram);
    }
    object_unref(OBJECT(bank));
    drain_call_rcu();
}

static uint8_t *ram_bytes(OwnedRam *bank)
{
    return memory_region_get_ram_ptr(&bank->ram);
}

static void clear_clients(OwnedRam *bank, hwaddr address)
{
    for (unsigned i = 0; i < ARRAY_SIZE(dirty_clients); i++) {
        memory_region_test_and_clear_dirty(&bank->ram, address, 4,
                                           dirty_clients[i]);
    }
}

static void assert_clients(OwnedRam *bank, hwaddr address, bool expected)
{
    for (unsigned i = 0; i < ARRAY_SIZE(dirty_clients); i++) {
        g_assert_cmpint(memory_region_test_and_clear_dirty(&bank->ram, address,
                                                           4, dirty_clients[i]),
                        ==, expected);
    }
}

/*
 * Missing fallback stores or incorrect masks corrupt independently specified
 * words; every changed RAM store must reach all enabled dirty clients.
 */
static void test_changed_masks(void)
{
    static const struct {
        uint32_t mask, value, expected;
    } cases[] = {
        { 0x000000ff, 0x99, 0x11223399 },
        { 0x0000ff00, 0x99, 0x11229944 },
        { 0x00ff0000, 0x99, 0x11993344 },
        { 0xff000000, 0x99, 0x99223344 },
        { 0xffffffff, 0xaabbccdd, 0xaabbccdd },
        { NV_PAVS_VOICE_PAR_STATE_NEW_VOICE, 1, 0x11323344 },
        { NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 0, 0x11023344 },
        { NV_PAVS_VOICE_PAR_OFFSET_CBO, 0xabcdef, 0x11abcdef },
    };
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, false);
    map_ram(bank, 0);
    for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
        stl_le_p(ram_bytes(bank) + word_address, 0x11223344);
        clear_clients(bank, word_address);
        mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram,
                                    word_address, cases[i].mask,
                                    cases[i].value);
        g_assert_cmphex((uint32_t)ldl_le_p(ram_bytes(bank) + word_address), ==,
                        cases[i].expected);
        assert_clients(bank, word_address, true);
    }
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 1);
}

/* Taking the physical fallback on admitted equal RAM needlessly dirties it. */
static void test_equal_clients(void)
{
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, false);
    map_ram(bank, 0);
    stl_le_p(ram_bytes(bank) + word_address, 0x11223344);
    clear_clients(bank, word_address);
    mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram, word_address,
                                0xff, 0x44);
    mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram, word_address,
                                0xffffffff, 0x11223344);
    g_assert_cmphex((uint32_t)ldl_le_p(ram_bytes(bank) + word_address), ==,
                    0x11223344);
    assert_clients(bank, word_address, false);
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 1);
}

/* Eliding conservative low-address or unaligned stores violates admission. */
static void test_fallback_guards(void)
{
    const hwaddr addresses[] = { 0x80, word_address + 1 };
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, false);
    map_ram(bank, 0);
    for (unsigned i = 0; i < ARRAY_SIZE(addresses); i++) {
        stl_le_p(ram_bytes(bank) + addresses[i], 0x11223344);
        clear_clients(bank, addresses[i]);
        mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram,
                                    addresses[i], 0xff, 0x44);
        g_assert_cmphex((uint32_t)ldl_le_p(ram_bytes(bank) + addresses[i]), ==,
                        0x11223344);
        assert_clients(bank, addresses[i], true);
    }
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 1);
}

/*
 * Reusing the old physical address or skipping the CAS mismatch writes the
 * wrong backing after the current mapping changes to another offset.
 */
static void test_alias_remap(void)
{
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, false);
    map_ram(bank, 0);
    stl_le_p(ram_bytes(bank) + word_address, 0x11223344);
    stl_le_p(ram_bytes(bank) + word_address + 4096, 0xaabbccdd);
    clear_clients(bank, word_address);
    clear_clients(bank, word_address + 4096);
    uint32_t old = ldl_le_phys(&address_space_memory, word_address);
    MemoryRegion alias;
    memory_region_init_alias(&alias, OBJECT(bank), "test-store-alias",
                             &bank->ram, word_address + 4096, 4);
    memory_region_add_subregion_overlap(get_system_memory(), word_address,
                                        &alias, 1);
    mcpx_apu_voice_store_masked_from_read(&address_space_memory, &bank->ram,
                                          word_address, 0xff, 0x44, old);
    g_assert_cmphex((uint32_t)ldl_le_p(ram_bytes(bank) + word_address + 4096),
                    ==, 0x11223344);
    g_assert_cmphex((uint32_t)ldl_le_p(ram_bytes(bank) + word_address), ==,
                    0x11223344);
    assert_clients(bank, word_address + 4096, true);
    assert_clients(bank, word_address, false);
    memory_region_del_subregion(get_system_memory(), &alias);
    object_unparent(OBJECT(&alias));
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 1);
}

/*
 * Treating any resolved RAM as the configured owner incorrectly elides a
 * physical fallback to a different RAM region, even if its word is equal.
 */
static void test_other_ram_owner(void)
{
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, false);
    OwnedRam *other = new_ram(&finalized, false);
    map_ram(bank, 0);
    stl_le_p(ram_bytes(bank) + word_address, 0x11223344);
    stl_le_p(ram_bytes(other) + word_address, 0x11223344);
    clear_clients(bank, word_address);
    clear_clients(other, word_address);
    uint32_t old = ldl_le_phys(&address_space_memory, word_address);
    map_ram(other, 1);
    mcpx_apu_voice_store_masked_from_read(&address_space_memory, &bank->ram,
                                          word_address, 0xff, 0x44, old);
    g_assert_cmphex(ldl_le_phys(&address_space_memory, word_address), ==,
                    0x11223344);
    assert_clients(other, word_address, true);
    assert_clients(bank, word_address, false);
    retire_ram(other);
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 2);
}

typedef struct ObservedIo {
    MemoryRegion region;
    unsigned writes;
    uint32_t last_value;
} ObservedIo;

static uint64_t io_read(void *opaque, hwaddr addr, unsigned size)
{
    g_assert_cmpuint(addr, ==, 0);
    g_assert_cmpuint(size, ==, 4);
    return 0x11223344;
}

static void io_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    ObservedIo *io = opaque;
    g_assert_cmpuint(addr, ==, 0);
    g_assert_cmpuint(size, ==, 4);
    io->writes++;
    io->last_value = value;
}

static const MemoryRegionOps io_ops = {
    .read = io_read,
    .write = io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void map_io(ObservedIo *io, int priority)
{
    memory_region_init_io(&io->region, NULL, &io_ops, io, "test-store-io", 4);
    memory_region_add_subregion_overlap(get_system_memory(), word_address,
                                        &io->region, priority);
}

static void retire_io(ObservedIo *io)
{
    memory_region_del_subregion(get_system_memory(), &io->region);
    drain_call_rcu();
    object_unparent(OBJECT(&io->region));
}

/*
 * Returning on read-value equality loses a same-value MMIO write when the
 * mapping becomes an observer after the original RAM read.
 */
static void test_mmio_remap(void)
{
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, false);
    map_ram(bank, 0);
    stl_le_p(ram_bytes(bank) + word_address, 0x11223344);
    uint32_t old = ldl_le_phys(&address_space_memory, word_address);
    ObservedIo io = { 0 };
    map_io(&io, 1);
    mcpx_apu_voice_store_masked_from_read(&address_space_memory, &bank->ram,
                                          word_address, 0xff, 0x44, old);
    g_assert_cmpuint(io.writes, ==, 1);
    g_assert_cmphex(io.last_value, ==, 0x11223344);
    mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram, word_address,
                                0xff, 0x99);
    g_assert_cmpuint(io.writes, ==, 2);
    g_assert_cmphex(io.last_value, ==, 0x11223399);
    g_assert_cmphex((uint32_t)ldl_le_p(ram_bytes(bank) + word_address), ==,
                    0x11223344);
    retire_io(&io);
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 1);
}

/*
 * A stale RAM extent or backing pointer must not hide MMIO exposed by shrink.
 */
static void test_ram_resize(void)
{
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, true);
    map_ram(bank, 0);
    stl_le_p(ram_bytes(bank) + word_address, 0x11223344);
    uint32_t old = ldl_le_phys(&address_space_memory, word_address);
    ObservedIo io = { 0 };
    map_io(&io, -1);
    g_assert_cmpint(
        qemu_ram_resize(bank->ram.ram_block, 0x100000, &error_fatal), ==, 0);
    mcpx_apu_voice_store_masked_from_read(&address_space_memory, &bank->ram,
                                          word_address, 0xff, 0x44, old);
    g_assert_cmpuint(io.writes, ==, 1);
    g_assert_cmphex(io.last_value, ==, 0x11223344);
    g_assert_cmpint(
        qemu_ram_resize(bank->ram.ram_block, 0x200000, &error_fatal), ==, 0);
    stl_le_p(ram_bytes(bank) + word_address, 0xaabbccdd);
    clear_clients(bank, word_address);
    mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram, word_address,
                                0xff, 0xdd);
    g_assert_cmphex(ldl_le_phys(&address_space_memory, word_address), ==,
                    0xaabbccdd);
    assert_clients(bank, word_address, false);
    g_assert_cmpuint(io.writes, ==, 1);
    retire_io(&io);
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 1);
}

typedef struct OrderedWriter {
    QemuEvent requested, finished;
    uint8_t *word;
} OrderedWriter;

static void *write_guest_word(void *opaque)
{
    OrderedWriter *writer = opaque;
    for (unsigned i = 0; i < 20000; i++) {
        qemu_event_wait(&writer->requested);
        qemu_event_reset(&writer->requested);
        /* The selected low byte remains 0x44; other fields change. */
        stl_le_p(writer->word, 0xaabbcc44);
        qemu_event_set(&writer->finished);
    }
    return NULL;
}

static void request_write(OrderedWriter *writer)
{
    qemu_event_reset(&writer->finished);
    qemu_event_set(&writer->requested);
    qemu_event_wait(&writer->finished);
}

/*
 * Dropping CAS revalidation loses the original masked-RMW result when a
 * writer precedes validation. Storing a stale word later loses a writer
 * ordered after completion. Events establish legal, non-racing C accesses.
 */
static void test_ordered_writers(void)
{
    unsigned finalized = 0;
    OwnedRam *bank = new_ram(&finalized, false);
    map_ram(bank, 0);
    OrderedWriter writer = { .word = ram_bytes(bank) + word_address };
    qemu_event_init(&writer.requested, false);
    qemu_event_init(&writer.finished, false);
    QemuThread thread;
    qemu_thread_create(&thread, "ordered-guest-store", write_guest_word,
                       &writer, QEMU_THREAD_JOINABLE);
    for (unsigned i = 0; i < 10000; i++) {
        stl_le_p(writer.word, 0x11223344);
        clear_clients(bank, word_address);
        uint32_t old = ldl_le_phys(&address_space_memory, word_address);
        request_write(&writer);
        mcpx_apu_voice_store_masked_from_read(&address_space_memory, &bank->ram,
                                              word_address, 0xff, 0x44, old);
        g_assert_cmphex((uint32_t)ldl_le_p(writer.word), ==, 0x11223344);
        assert_clients(bank, word_address, true);

        clear_clients(bank, word_address);
        mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram,
                                    word_address, 0xff, 0x44);
        assert_clients(bank, word_address, false);
        request_write(&writer);
        g_assert_cmphex((uint32_t)ldl_le_p(writer.word), ==, 0xaabbcc44);
    }
    bql_unlock();
    qemu_thread_join(&thread);
    bql_lock();
    qemu_event_destroy(&writer.finished);
    qemu_event_destroy(&writer.requested);
    retire_ram(bank);
    g_assert_cmpuint(qatomic_read(&finalized), ==, 1);
}

/*
 * Retaining a host word or cached value across legal owner retirement breaks
 * the next backing's independently specified contents or leaks its owner.
 */
static void test_owner_retirement(void)
{
    unsigned finalized = 0;
    for (unsigned generation = 0; generation < 64; generation++) {
        OwnedRam *bank = new_ram(&finalized, false);
        map_ram(bank, 0);
        stl_le_p(ram_bytes(bank) + word_address,
                 generation % 2 ? 0x55667788 : 0x11223344);
        mcpx_apu_voice_store_masked(&address_space_memory, &bank->ram,
                                    word_address, 0xff, 0x99);
        g_assert_cmphex(ldl_le_phys(&address_space_memory, word_address), ==,
                        generation % 2 ? 0x55667799 : 0x11223399);
        retire_ram(bank);
        g_assert_cmpuint(qatomic_read(&finalized), ==, generation + 1);
    }
}

int __wrap_main(int argc, char **argv);
int __wrap_main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qemu_init_cpu_loop();
    bql_lock();
    module_call_init(MODULE_INIT_QOM);
    type_register_static(&owned_ram_type);
    current_machine = MACHINE(object_new("xbox-machine"));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_new_container(OBJECT(current_machine), "unattached");
    cpu_exec_init_all();
    g_test_add_func("/mcpx-apu/voice-store/changed-masks", test_changed_masks);
    g_test_add_func("/mcpx-apu/voice-store/equal-clients", test_equal_clients);
    g_test_add_func("/mcpx-apu/voice-store/fallback-guards",
                    test_fallback_guards);
    g_test_add_func("/mcpx-apu/voice-store/alias-remap", test_alias_remap);
    g_test_add_func("/mcpx-apu/voice-store/other-ram-owner",
                    test_other_ram_owner);
    g_test_add_func("/mcpx-apu/voice-store/mmio-remap", test_mmio_remap);
    g_test_add_func("/mcpx-apu/voice-store/ram-resize", test_ram_resize);
    g_test_add_func("/mcpx-apu/voice-store/ordered-writers",
                    test_ordered_writers);
    g_test_add_func("/mcpx-apu/voice-store/owner-retirement",
                    test_owner_retirement);
    return g_test_run();
}
