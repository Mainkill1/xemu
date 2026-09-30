/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#if STAGE != 0
#define CONFIG_XEMU_TCG_JUMP_CACHE_PROBE 1
#if STAGE == 6 || STAGE == 7
#include "jump-cache-probe.h"
#else
#include "accel/tcg/jump-cache-probe-lookup.h"
#endif
#endif
#include "accel/tcg/tb-cpu-state.h"
#include "accel/tcg/tb-jmp-cache.h"
#include "accel/tcg/tb-hash.h"
#include "qemu/timer.h"

static uint64_t ns(clockid_t id)
{
    struct timespec t;
    clock_gettime(id, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    uint64_t iterations = strtoull(argv[1], NULL, 10);
    unsigned work = strtoul(argv[2], NULL, 10);
    CPUJumpCache *jc = g_new0(CPUJumpCache, 1);
    TCGTBCPUState s = { .pc = 0x81234560, .cs_base = 0x12340000,
                       .flags = 0x80000001, .cflags = CF_PCREL | 32 };
    TranslationBlock tb = { .pc = s.pc, .cs_base = s.cs_base,
                            .flags = s.flags, .cflags = s.cflags };
    unsigned hash = tb_jmp_cache_hash_func(s.pc);
    jc->array[hash].pc = s.pc;
    qatomic_set(&jc->array[hash].tb, &tb);
#if STAGE != 0
    jc->probe = tcg_jump_cache_probe_new(
#if STAGE == 1 || STAGE == 7
        "off"
#elif STAGE == 5
        "timing"
#else
        "counters"
#endif
    );
#endif
    uint64_t checksum = 1;
    uint64_t wall = ns(CLOCK_MONOTONIC), cpu = ns(CLOCK_PROCESS_CPUTIME_ID);
    for (uint64_t i = 0; i < iterations; i++) {
        /* A generated TB can clobber memory: forbid hoisting cache validation. */
        asm volatile("" ::: "memory");
#if STAGE != 0
        TCGJumpCacheProbe *probe = jc->probe;
#if STAGE != 2 && STAGE != 3
        uint64_t start = 0;
#endif
        if (unlikely(probe)) {
#if STAGE == 2 || STAGE == 3
            probe->owner.sequence++;
#else
            start = tcg_jump_cache_probe_lookup_begin(probe);
#endif
        }
#endif
        TranslationBlock *found = tcg_jump_cache_lookup(jc, hash, s);
        assert(found);
#if STAGE != 0
        if (unlikely(probe)) {
#if STAGE == 3
            probe->owner.cost[TCG_JUMP_CACHE_HIT].calls++;
#elif STAGE != 2
            tcg_jump_cache_probe_lookup_end(probe, TCG_JUMP_CACHE_HIT, start);
#endif
        }
#endif
        checksum += found->flags;
        for (unsigned j = 0; j < work; j++) {
            checksum = (checksum ^ (checksum >> 11)) * 6364136223846793005ULL + j;
        }
    }
    cpu = ns(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    wall = ns(CLOCK_MONOTONIC) - wall;
    uint64_t started = 0, completed = 0, samples = 0;
#if STAGE != 0
#if STAGE != 6 && STAGE != 7
    tcg_jump_cache_probe_publish_owner(jc->probe);
#endif
    TCGJumpCacheProbeStats stats;
    tcg_jump_cache_probe_snapshot(jc->probe, &stats);
    started = stats.lookup_sequence;
    completed = stats.lookup[TCG_JUMP_CACHE_HIT].calls;
    samples = stats.lookup[TCG_JUMP_CACHE_HIT].samples;
#if STAGE != 1 && STAGE != 7
    assert(started == iterations);
#if STAGE != 2
    assert(completed == iterations);
#else
    assert(completed == 0);
#endif
#endif
    tcg_jump_cache_probe_free(jc->probe);
#endif
    printf("{\"stage\":%d,\"iterations\":%" PRIu64 ",\"work\":%u,"
           "\"cpu_ns\":%" PRIu64 ",\"wall_ns\":%" PRIu64 ","
           "\"checksum\":%" PRIu64 ",\"started\":%" PRIu64 ","
           "\"completed\":%" PRIu64 ",\"timing_samples\":%" PRIu64 "}\n",
           STAGE, iterations, work, cpu, wall, checksum, started, completed, samples);
    g_free(jc);
    return 0;
}
