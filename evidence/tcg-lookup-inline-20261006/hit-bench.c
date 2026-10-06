#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "exec/cpu-common.h"
#include "exec/translation-block.h"
#include "accel/tcg/tb-jmp-cache.h"
#include "accel/tcg/tb-hash.h"
#include "exec/target_page.h"

const TargetPageBits target_page = { .decided = true, .bits = 12,
                                    .mask = ~(uintptr_t)4095 };
bool one_insn_per_tb;
extern const void *helper_lookup_tb_ptr_i32(CPUArchState *, uint32_t,
                                           uint64_t, uint32_t);
/* Any unmodelled slow path aborts; this fixture measures valid hits only. */
#include "accel/tcg/tb-context.h"
#include "qemu/log.h"
unsigned qemu_loglevel;
#include "disas/disas.h"
TBContext tb_ctx;
const void *tcg_code_gen_epilogue;
tb_page_addr_t get_page_addr_code_hostp(CPUArchState *, vaddr, void **);
tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, vaddr addr,
                                       void **hostp) { abort(); }
void *qht_lookup_custom(const struct qht *ht, const void *u, uint32_t hash,
                        qht_lookup_func_t fn) { abort(); }
FILE *qemu_log_trylock(void) { abort(); }
void qemu_log_unlock(FILE *f) { abort(); }
void qemu_log(const char *fmt, ...) { abort(); }
bool qemu_log_in_addr_range(uint64_t addr) { abort(); }
const char *lookup_symbol(uint64_t addr) { abort(); }
void cpu_dump_state(CPUState *cpu, FILE *f, int flags) { abort(); }
G_NORETURN void cpu_loop_exit(CPUState *cpu) { abort(); }
static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
int main(void)
{
    CPUState *cpu = calloc(1, sizeof(*cpu) + 64);
    CPUJumpCache *cache = calloc(1, sizeof(*cache));
    TranslationBlock tb = { 0 };
    const uint32_t pc = 0x123400;
    unsigned h = tb_jmp_cache_hash_func(pc);
    cpu->tb_jmp_cache = cache;
    tb.pc = pc;
    tb.tc.ptr = (void *)0xabcdef00;
    cache->array[h].pc = pc;
    cache->array[h].tb = &tb;
    const void *result = NULL;
    uint64_t start = now_ns();
    for (unsigned i = 0; i < 20000000; i++) {
        result = helper_lookup_tb_ptr_i32(cpu_env(cpu), pc, 0, 0);
    }
    uint64_t elapsed = now_ns() - start;
    assert(result == tb.tc.ptr && cpu->neg.can_do_io);
    printf("%.3f\n", (double)elapsed / 20000000);
    free(cache);
    free(cpu);
}
