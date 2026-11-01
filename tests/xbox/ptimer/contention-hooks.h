/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_CONTENTION_HOOKS_H
#define NV2A_CONTENTION_HOOKS_H
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/thread.h"

void contention_mutex_lock(QemuMutex *mutex, const char *file, int line);
void contention_mutex_unlock(QemuMutex *mutex, const char *file, int line);
void contention_cond_wait(QemuCond *cond, QemuMutex *mutex, const char *file,
                          int line);
uint32_t contention_cas(uint32_t *ptr, uint32_t old, uint32_t next);

/* Preserve the production atomic operation; pause only immediately before it.
 */
static inline uint32_t contention_real_cas(uint32_t *ptr, uint32_t old,
                                           uint32_t next)
{
    return qatomic_cmpxchg(ptr, old, next);
}
#ifndef NV2A_CONTENTION_COST
#undef qatomic_cmpxchg
#define qatomic_cmpxchg(ptr, old, next) contention_cas(ptr, old, next)
#undef qemu_mutex_lock
#undef qemu_mutex_unlock
#undef qemu_cond_wait
#define qemu_mutex_lock(m) contention_mutex_lock(m, __FILE__, __LINE__)
#define qemu_mutex_unlock(m) contention_mutex_unlock(m, __FILE__, __LINE__)
#define qemu_cond_wait(c, m) contention_cond_wait(c, m, __FILE__, __LINE__)
#endif
#endif
