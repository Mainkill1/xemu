/* Standalone diagnostic output; no emulator logger/thread is installed. */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
void error_report(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void qemu_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#include "qemu/main-loop.h"
bool mutex_is_bql(QemuMutex *m) { return false; }
void bql_update_status(bool locked) { abort(); }

QemuMutexLockFunc qemu_mutex_lock_func = qemu_mutex_lock_impl;
