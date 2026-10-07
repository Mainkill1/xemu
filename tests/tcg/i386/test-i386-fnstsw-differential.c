/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compare FNSTSW AX with the helper-backed FNSTSW memory form. */

#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

typedef struct __attribute__((packed)) FpuEnvironment {
    uint16_t control;
    uint16_t reserved_control;
    uint16_t status;
    uint16_t reserved_status;
    uint16_t tags;
    uint16_t reserved_tags;
    uint32_t fip;
    uint16_t fcs;
    uint16_t opcode;
    uint32_t fdp;
    uint16_t fds;
    uint16_t reserved_data;
} FpuEnvironment;

_Static_assert(sizeof(FpuEnvironment) == 28, "FNSTENV layout");

static int test_pointer_preservation(void)
{
    FpuEnvironment before, after;
    uint32_t status;

    /* No C call or other x87 instruction intervenes at the status boundary. */
    __asm__ volatile("fninit\n\tfld1\n\tfnstenv %1\n\t"
                     "movl $0xa5b60000, %%eax\n\tfnstsw %%ax\n\t"
                     "fnstenv %2\n\tfninit"
                     : "=&a"(status), "=m"(before), "=m"(after)
                     : : "st", "memory");
    if (status != 0xa5b63800 || before.fip != after.fip ||
        before.fcs != after.fcs || before.fdp != after.fdp ||
        before.fds != after.fds) {
        printf("FAIL AX pointer eax=%08x fip=%08x/%08x fcs=%04x/%04x\n",
               status, before.fip, after.fip, before.fcs, after.fcs);
        return 1;
    }

    /* Retain the unchanged memory form as a separately executed reference. */
    uint16_t memory_status;
    __asm__ volatile("fninit\n\tfld1\n\tfnstenv %0\n\t"
                     "fnstsw %2\n\tfnstenv %1\n\tfninit"
                     : "=m"(before), "=m"(after), "=m"(memory_status)
                     : : "st", "memory");
    if (memory_status != 0x3800 || before.fip != after.fip ||
        before.fcs != after.fcs || before.fdp != after.fdp ||
        before.fds != after.fds) {
        printf("FAIL memory pointer status=%04x fip=%08x/%08x\n",
               memory_status, before.fip, after.fip);
        return 1;
    }
    return 0;
}

static uint32_t read_ax_status(void)
{
    uint32_t status = 0xa5b60000;

    __asm__ volatile("fnstsw %%ax" : "+a"(status));
    return status;
}

static volatile sig_atomic_t fault_count;
static void *fault_page;

static void fault_handler(int signal, siginfo_t *info, void *opaque)
{
    ucontext_t *context = opaque;

    if (signal != SIGSEGV || info->si_addr != fault_page) {
        _exit(2);
    }
    /* The deliberate MOV EDX,[ECX/RCX] is exactly two bytes on both hosts. */
#ifdef __x86_64__
    context->uc_mcontext.gregs[REG_RIP] += 2;
#else
    context->uc_mcontext.gregs[REG_EIP] += 2;
#endif
    fault_count++;
}

static int test_fault_checkpoint(void)
{
    struct sigaction action = { .sa_sigaction = fault_handler,
                                .sa_flags = SA_SIGINFO };
    struct sigaction previous;
    long page_size = sysconf(_SC_PAGESIZE);
    FpuEnvironment environment;
    double value;
    uint32_t status;

    if (page_size <= 0) {
        perror("FNSTSW page size");
        return 1;
    }
    sigemptyset(&action.sa_mask);
    fault_page =
        mmap(NULL, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (fault_page == MAP_FAILED) {
        perror("FNSTSW fault page");
        return 1;
    }
    if (sigaction(SIGSEGV, &action, &previous) != 0) {
        perror("FNSTSW fault handler");
        munmap(fault_page, page_size);
        return 1;
    }

    /*
     * Dirty cached ST0, then status and fault in one translated sequence.
     * Signal return must restore the checkpointed value, TOP and tags.
     */
    __asm__ volatile("fninit\n\t"
                     "fld1\n\t"
                     "fld1\n\t"
                     "faddp\n\t"
                     "fld1\n\t"
                     "faddp\n\t"
                     "movl $0xa5b60000, %%eax\n\t"
                     "fnstsw %%ax\n\t"
                     ".byte 0x8b, 0x11\n\t"
                     "fnstenv %1\n\t"
                     "fstpl %2\n\t"
                     "fninit"
                     : "=&a"(status), "=m"(environment), "=m"(value)
                     : "c"(fault_page)
                     : "edx", "st", "st(1)", "memory");

    sigaction(SIGSEGV, &previous, NULL);
    munmap(fault_page, page_size);
    if (fault_count != 1 || status != 0xa5b63800 || value != 3.0 ||
        (environment.status & 0x3800) != 0x3800 || environment.tags != 0x3fff) {
        printf("FAIL fault checkpoint count=%d eax=%08x value=%g "
               "status=%04x tags=%04x\n",
               fault_count, status, value, environment.status,
               environment.tags);
        return 1;
    }
    return 0;
}

static uint16_t read_memory_status(void)
{
    uint16_t status;

    __asm__ volatile("fnstsw %0" : "=m"(status));
    return status;
}

/* Compare AX and helper-backed memory status between FP operations. */
#define STATUS_ROUNDING_BODY(status_op, boundary) \
    __asm__ volatile("fninit\n\tfldcw %[cw]\n\t" \
                     "fldl %[value]\n\tfaddl %[step]\n\t" \
                     status_op "\n\t" boundary \
                     "faddl %[step]\n\t" status_op "\n\t" \
                     "faddl %[step]\n\tfstpl %[out]\n\tfninit" \
                     : [out] "=m"(result), [status] "=m"(status) \
                     : [cw] "m"(control), [value] "m"(value), \
                       [step] "m"(step), [changed] "m"(changed), \
                       [mxcsr] "m"(mxcsr) \
                     : "eax", "st", "st(1)", "st(2)", "st(3)", "st(4)", \
                       "st(5)", "st(6)", "st(7)", "memory")

#define ROUNDING_PROBE(name, status_op) \
static double name(uint16_t control, double value, double step, \
                   unsigned boundary) \
{ \
    double result; \
    uint16_t status; \
    uint16_t changed = control ^ 0x0c00; \
    uint32_t mxcsr = 0x1f80 | (((control >> 10) ^ 3) & 3) << 13; \
    switch (boundary) { \
    case 0: \
        STATUS_ROUNDING_BODY(status_op, ""); \
        break; \
    case 1: \
        STATUS_ROUNDING_BODY(status_op, "fldcw %[changed]\n\t"); \
        break; \
    case 2: \
        STATUS_ROUNDING_BODY(status_op, "frndint\n\t"); \
        break; \
    default: \
        STATUS_ROUNDING_BODY(status_op, "ldmxcsr %[mxcsr]\n\t"); \
        break; \
    } \
    return result; \
}

ROUNDING_PROBE(rounding_ax, "fnstsw %%ax")
ROUNDING_PROBE(rounding_memory, "fnstsw %[status]")

static int test_rounding_boundaries(void)
{
    static const uint16_t precision[] = { 0, 0x200, 0x300 };
    uint32_t saved_mxcsr;
    unsigned cases = 0;

    __asm__ volatile("stmxcsr %0" : "=m"(saved_mxcsr));
    for (unsigned pc = 0; pc < 3; pc++) {
        for (unsigned rc = 0; rc < 4; rc++) {
            for (unsigned sign = 0; sign < 2; sign++) {
                for (unsigned boundary = 0; boundary < 4; boundary++) {
                    uint16_t control = 0x7f | precision[pc] | (rc << 10);
                    double value = sign ? -1.0 : 1.0;
                    double step = pc ? 1.0 / 9007199254740992.0 :
                                       1.0 / 16777216.0;
                    union { double value; uint64_t bits; } ax, memory;

                    ax.value = rounding_ax(control, value, step, boundary);
                    memory.value = rounding_memory(control, value, step,
                                                   boundary);
                    cases++;
                    if (ax.bits != memory.bits) {
                        printf("FAIL rounding pc=%u rc=%u "
                               "sign=%u boundary=%u\n",
                               pc, rc, sign, boundary);
                        __asm__ volatile("ldmxcsr %0" : : "m"(saved_mxcsr));
                        return 1;
                    }
                }
            }
        }
    }
    __asm__ volatile("ldmxcsr %0" : : "m"(saved_mxcsr));
    printf("FNSTSW rounding boundaries: %u PASS\n", cases);
    return 0;
}
#undef ROUNDING_PROBE
#undef STATUS_ROUNDING_BODY

int main(void)
{
    if (test_pointer_preservation() || test_fault_checkpoint() ||
        test_rounding_boundaries()) {
        return 1;
    }
    static const uint16_t condition_bits[] = {
        0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700,
        0x4000, 0x4100, 0x4200, 0x4300, 0x4400, 0x4500, 0x4600, 0x4700,
    };
    static const uint16_t summary_bits[] = {
        0x0000,
        0x0080,
        0x8000,
        0x8080,
    };
    static const uint16_t tag_words[] = {
        0x0000,
        0x5555,
        0xaaaa,
        0xffff,
    };
    static const uint16_t exception_masks[] = {
        0x0000, 0x003f, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020,
    };
    FpuEnvironment original;

    __asm__ volatile("fnstenv %0" : "=m"(original));
    for (unsigned int mask = 0;
         mask < sizeof(exception_masks) / sizeof(exception_masks[0]); mask++) {
        for (unsigned int top = 0; top < 8; top++) {
            for (unsigned int condition = 0;
                 condition < sizeof(condition_bits) / sizeof(condition_bits[0]);
                 condition++) {
                for (unsigned int flags = 0; flags < 128; flags++) {
                    for (unsigned int summary = 0;
                         summary <
                         sizeof(summary_bits) / sizeof(summary_bits[0]);
                         summary++) {
                        for (unsigned int tags = 0;
                             tags < sizeof(tag_words) / sizeof(tag_words[0]);
                             tags++) {
                            FpuEnvironment test = original;
                            test.control = (original.control & ~0x003f) |
                                           exception_masks[mask];
                            test.status =
                                (uint16_t)(condition_bits[condition] | flags |
                                           summary_bits[summary] | (top << 11));
                            test.tags = tag_words[tags];
                            __asm__ volatile("fnclex");
                            __asm__ volatile("fldenv %0" : : "m"(test));

                            uint32_t ax = read_ax_status();
                            uint16_t memory = read_memory_status();
                            if ((uint16_t)ax != memory ||
                                (ax & 0xffff0000) != 0xa5b60000) {
                                printf(
                                    "FAIL mask=%04x top=%u cc=%04x flags=%02x "
                                    "summary=%04x tags=%04x eax=%08x "
                                    "mem=%04x\n",
                                    exception_masks[mask], top,
                                    condition_bits[condition], flags,
                                    summary_bits[summary], tag_words[tags], ax,
                                    memory);
                                __asm__ volatile("fnclex");
                                __asm__ volatile("fldenv %0" : : "m"(original));
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    __asm__ volatile("fnclex");
    __asm__ volatile("fldenv %0" : : "m"(original));
    return 0;
}
