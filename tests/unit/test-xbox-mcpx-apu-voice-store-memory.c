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
#include "hw/xbox/mcpx/apu/vp/voice-store.h"

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

typedef struct VoiceStoreFixture {
    AddressSpace as;
    MemoryRegion root;
    MemoryRegion ram;
    MemoryRegion overlay;
    MemoryRegion partial;
    Object *owner;
    RAMBlock block;
    uint8_t *bytes;
    unsigned overlay_writes;
    unsigned partial_writes;
    DirtyMemoryBlocks *dirty;
} VoiceStoreFixture;

static uint64_t overlay_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0x12345678;
}

static void overlay_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned size)
{
    VoiceStoreFixture *fixture = opaque;

    fixture->overlay_writes++;
}

static uint64_t partial_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0x5678;
}

static void partial_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned size)
{
    VoiceStoreFixture *fixture = opaque;

    fixture->partial_writes++;
}

static const MemoryRegionOps overlay_ops = {
    .read = overlay_read,
    .write = overlay_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps partial_ops = {
    .read = partial_read,
    .write = partial_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void test_real_address_space_ram_and_overlay(void)
{
    static VoiceStoreFixture f;
    const hwaddr base = 0x100000;
    const size_t ram_size = 0x200000;

    f.bytes = g_malloc0(ram_size);
    f.owner = object_new(TYPE_CONTAINER);
    memory_region_init(&f.root, f.owner, "voice-test-root",
                       ram_size + 0x1000);
    memory_region_init(&f.ram, f.owner, "voice-test-ram", ram_size);
    f.ram.ram = true;
    f.ram.terminates = true;
    f.block.host = f.bytes;
    f.block.used_length = ram_size;
    f.block.max_length = ram_size;
    f.block.mr = &f.ram;
    f.ram.ram_block = &f.block;
    memory_region_add_subregion(&f.root, 0, &f.ram);
    memory_region_init_io(&f.overlay, f.owner, &overlay_ops, &f,
                          "voice-test-overlay", 4);
    memory_region_add_subregion_overlap(&f.root, base + 8,
                                         &f.overlay, 1);
    memory_region_init_io(&f.partial, f.owner, &partial_ops, &f,
                          "voice-test-partial", 2);
    memory_region_add_subregion_overlap(&f.root, base + 16,
                                         &f.partial, 1);
    address_space_init(&f.as, &f.root, "voice-test-as");

    f.dirty = g_malloc0(sizeof(*f.dirty) + sizeof(f.dirty->blocks[0]));
    f.dirty->blocks[0] = g_malloc0(BITS_TO_LONGS(DIRTY_MEMORY_BLOCK_SIZE) *
                                    sizeof(unsigned long));
    ram_list.dirty_memory[DIRTY_MEMORY_VGA] = f.dirty;
    ram_list.num_dirty_blocks = 1;
    memory_region_set_log(&f.ram, true, DIRTY_MEMORY_VGA);

    stl_le_p(f.bytes + base, 0x12345678);
    g_assert_false(test_bit(base >> TARGET_PAGE_BITS,
                            f.dirty->blocks[0]));
    mcpx_apu_voice_store_masked(&f.as, &f.ram, base, 0xff, 0x78);
    g_assert_cmphex(ldl_le_p(f.bytes + base), ==, 0x12345678);
    g_assert_false(test_bit(base >> TARGET_PAGE_BITS,
                            f.dirty->blocks[0]));

    mcpx_apu_voice_store_masked(&f.as, &f.ram, base, 0xff, 0x34);
    g_assert_cmphex(ldl_le_p(f.bytes + base), ==, 0x12345634);
    g_assert_true(test_bit(base >> TARGET_PAGE_BITS,
                           f.dirty->blocks[0]));

    mcpx_apu_voice_store_masked(&f.as, &f.ram, base + 8, 0xff, 0x78);
    g_assert_cmpuint(f.overlay_writes, ==, 1);

    mcpx_apu_voice_store_masked(&f.as, &f.ram, base + 16, 0xff, 0x78);
    g_assert_cmpuint(f.partial_writes, ==, 1);

    /* The guest changes RAM after the APU read and before validation. */
    stl_le_p(f.bytes + base, 0xaabbccdd);
    mcpx_apu_voice_store_masked_from_read(&f.as, &f.ram, base, 0xff,
                                           0x34, 0x12345634);
    g_assert_cmphex(ldl_le_p(f.bytes + base), ==, 0x12345634);

    /* FlatViews may outlive a callback; fixture storage lasts to exit. */
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    cpu_exec_init_all();
    rust_bql_mock_lock();
    g_test_add_func("/xbox/apu/voice-store/real-address-space",
                    test_real_address_space_ram_and_overlay);
    return g_test_run();
}
