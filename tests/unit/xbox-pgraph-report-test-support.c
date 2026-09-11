/*
 * Owned-memory support for NV2A report integration tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "xbox-pgraph-report-test-support.h"

bool report_test_guarded_buffer_init(ReportTestGuardedBuffer *buffer)
{
#ifdef _WIN32
    DWORD old_protect;

    buffer->page_size = qemu_real_host_page_size();
    buffer->mapping = VirtualAlloc(NULL, 2 * buffer->page_size,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (buffer->mapping == NULL) {
        return false;
    }
    buffer->guard = buffer->mapping + buffer->page_size;
    if (!VirtualProtect(buffer->guard, buffer->page_size, PAGE_NOACCESS,
                        &old_protect)) {
        VirtualFree(buffer->mapping, 0, MEM_RELEASE);
        memset(buffer, 0, sizeof(*buffer));
        return false;
    }
#else
    buffer->page_size = qemu_real_host_page_size();
    buffer->mapping = mmap(NULL, 2 * buffer->page_size,
                           PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                           -1, 0);
    if (buffer->mapping == MAP_FAILED) {
        memset(buffer, 0, sizeof(*buffer));
        return false;
    }
    buffer->guard = buffer->mapping + buffer->page_size;
    if (mprotect(buffer->guard, buffer->page_size, PROT_NONE) != 0) {
        munmap(buffer->mapping, 2 * buffer->page_size);
        memset(buffer, 0, sizeof(*buffer));
        return false;
    }
#endif
    return true;
}

void report_test_guarded_buffer_destroy(ReportTestGuardedBuffer *buffer)
{
    if (buffer->mapping == NULL) {
        return;
    }
#ifdef _WIN32
    VirtualFree(buffer->mapping, 0, MEM_RELEASE);
#else
    munmap(buffer->mapping, 2 * buffer->page_size);
#endif
    memset(buffer, 0, sizeof(*buffer));
}

void report_test_memory_region_set_size(MemoryRegion *region, uint64_t size)
{
    /*
     * The integration target is deliberately below full device/QOM setup.
     * The production wrapper still calls memory_region_size(), whose bounded
     * test implementation below reads this owned region's normal size field.
     */
    memset(region, 0, sizeof(*region));
    region->size = int128_make64(size);
}

/* The integration units need only the public size query on owned regions. */
uint64_t memory_region_size(MemoryRegion *region)
{
    return int128_get64(region->size);
}
