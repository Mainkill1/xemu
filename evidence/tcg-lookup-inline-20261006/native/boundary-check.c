#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "exec/cpu-common.h"
#include "exec/translation-block.h"
#include "accel/tcg/tb-jmp-cache.h"
#include "accel/tcg/tb-hash.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ops.h"
#include "internal-common.h"
#include <setjmp.h>

const TargetPageBits target_page = { .decided = true, .bits = 12,
                                    .mask = ~(uintptr_t)4095 };
bool one_insn_per_tb;
extern const void *helper_lookup_tb_ptr_i32(CPUArchState *, uint32_t,
                                           uint64_t, uint32_t);
/* Any unmodelled slow path aborts; this fixture measures valid hits only. */
#include "accel/tcg/tb-context.h"
#include "qemu/log.h"
unsigned qemu_loglevel;
static unsigned page_reads, log_checks, exits, cases;
static jmp_buf exit_jump;
#include "disas/disas.h"
TBContext tb_ctx;
const void *tcg_code_gen_epilogue;
tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, vaddr addr,
                                       void **hostp) { assert(env_cpu(env)->neg.can_do_io); page_reads++; return -1; }
void *qht_lookup_custom(const struct qht *ht, const void *u, uint32_t hash,
                        qht_lookup_func_t fn) { abort(); }
FILE *qemu_log_trylock(void) { abort(); }
void qemu_log_unlock(FILE *f) { abort(); }
void qemu_log(const char *fmt, ...) { abort(); }
bool qemu_log_in_addr_range(uint64_t addr) { log_checks++; return false; }
const char *lookup_symbol(uint64_t addr) { abort(); }
void cpu_dump_state(CPUState *cpu, FILE *f, int flags) { abort(); }
G_NORETURN void cpu_loop_exit(CPUState *cpu) { assert(cpu->neg.can_do_io); exits++; longjmp(exit_jump, 1); }

extern const void *helper_lookup_tb_ptr(CPUArchState *);
static TCGTBCPUState state;
static TCGTBCPUState get_state(CPUState *cpu) { return state; }

int main(void)
{
    CPUState *cpu = calloc(1, sizeof(*cpu) + 64);
    CPUJumpCache *cache = calloc(1, sizeof(*cache));
    TCGCPUOps ops = { .get_tb_cpu_state = get_state };
    CPUClass klass = { .tcg_ops = &ops };
    TranslationBlock tb = { 0 };
    CPUBreakpoint bp = { .flags = BP_GDB };
    const uint32_t eip = 0x23400;
    const uint64_t base = 0xffff0000ULL;
    const uint32_t pc = (uint32_t)(base + eip);
    const uint32_t flags = 0x1234;
    unsigned h = tb_jmp_cache_hash_func(pc);
    cpu->cc = &klass;
    cpu->tb_jmp_cache = cache;
    cpu->tcg_cflags = CF_NO_GOTO_TB;
    QTAILQ_INIT(&cpu->breakpoints);
    tcg_code_gen_epilogue = (void *)0xdead1000;
    state = (TCGTBCPUState){ .pc = pc, .cs_base = base, .flags = flags };
    for (unsigned generic = 0; generic < 2; generic++) {
        for (unsigned test = 0; test < 11; test++) {
            tb.pc = pc;
            tb.cs_base = base;
            tb.flags = flags;
            tb.cflags = cpu->tcg_cflags;
            tb.tc.ptr = (void *)0xabcdef00;
            cache->array[h].pc = pc;
            cache->array[h].tb = &tb;
            cpu->neg.can_do_io = false;
            cpu->singlestep_enabled = 0;
            qemu_loglevel = 0;
            QTAILQ_INIT(&cpu->breakpoints);
            switch (test) {
            case 1: cache->array[h].pc++; break;
            case 2: tb.cs_base++; break;
            case 3: tb.flags++; break;
            case 4: tb.cflags ^= CF_NO_GOTO_TB; break;
            case 5: cache->array[h].tb = NULL; break;
            case 6: tb.cflags |= CF_INVALID; break;
            case 7: qemu_loglevel = CPU_LOG_EXEC; break;
            case 8:
                bp.pc = pc + 1;
                QTAILQ_INSERT_HEAD(&cpu->breakpoints, &bp, entry);
                break;
            case 9:
                cpu->singlestep_enabled = 1;
                tb.cflags = curr_cflags(cpu);
                break;
            case 10:
                bp.pc = pc;
                QTAILQ_INSERT_HEAD(&cpu->breakpoints, &bp, entry);
                break;
            }
            page_reads = log_checks = exits = 0;
            if (!setjmp(exit_jump)) {
                const void *r = generic ? helper_lookup_tb_ptr(cpu_env(cpu)) :
                    helper_lookup_tb_ptr_i32(cpu_env(cpu), eip, base, flags);
                bool miss = (test >= 1 && test <= 6) || test == 8;
                assert(test != 10);
                assert(r == (miss ? tcg_code_gen_epilogue : tb.tc.ptr));
                assert(page_reads == (unsigned)miss);
                assert(log_checks == (test == 7));
                assert(!exits);
            } else {
                assert(test == 10 && exits == 1 && !page_reads);
                assert(cpu->exception_index == EXCP_DEBUG);
            }
            assert(cpu->neg.can_do_io);
            cases++;
        }
    }
    printf("%u production helper boundary cases passed\n", cases);
    free(cache);
    free(cpu);
}
