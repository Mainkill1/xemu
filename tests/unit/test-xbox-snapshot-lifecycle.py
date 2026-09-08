#!/usr/bin/env python3
"""Compile actual snapshot/NV2A lifecycle functions with deterministic I/O faults.

No GPU or guest performance is modeled. The doubles enforce lock ownership and
quiescing before serialization; native emulator qualification remains separate.
"""
import argparse
import pathlib
import subprocess
import tempfile


def function(text, name):
    import re
    match = re.search(r"(?:static\s+)?(?:bool|void|int)\s+" + name + r"\s*\([^;]*?\)\s*\{", text)
    if not match:
        return ""
    start = match.start()
    pos = text.index("{", match.start())
    depth = 1
    end = pos + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parents[2])
args = parser.parse_args()
save = (args.source_root / 'migration/savevm.c').read_text()
nv = (args.source_root / 'hw/xbox/nv2a/nv2a.c').read_text()
handler = function(nv, 'nv2a_vm_state_change')
post = function(nv, 'nv2a_post_save')
helpers = '\n'.join(function(nv, n) for n in ['nv2a_savevm_quiesce', 'nv2a_savevm_prepare', 'nv2a_savevm_finish'])
assert handler and function(save, 'save_snapshot')
prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#define XBOX 1
#define g_autoptr(T) T *
#define g_autofree
#define GLOBAL_STATE_CODE() ((void)0)
#define qatomic_set(p,v) (*(p)=(v))
#define qatomic_read(p) (*(p))
#define QEMU_CLOCK_VIRTUAL 0
#define REPLAY_MODE_NONE 0
int replay_mode;
typedef int Error;
typedef int BlockDriverState;
typedef int QEMUFile;
typedef int QIOChannelFile;
#define QIO_CHANNEL(p) (p)
#define OBJECT(p) (p)
typedef int strList;
typedef int GDateTime;
typedef enum { RUN_STATE_PAUSED, RUN_STATE_RUNNING, RUN_STATE_SAVE_VM,
               RUN_STATE_RESTORE_VM, RUN_STATE_SHUTDOWN } RunState;
typedef struct { struct { bool halt; } pfifo;
                 bool savevm_locked, savevm_explicit, savevm_previous_halt; } NV2AState;
typedef struct { int64_t date_sec, date_nsec, vm_clock_nsec;
                 uint64_t icount; char name[128]; } QEMUSnapshotInfo;
static NV2AState device, *g_nv2a = &device;
static bool held, bql = true;
static RunState state;
static int failure, preparations, finishes, writes, drains, storage, closes;
static void nv2a_vm_state_change(void *, bool, RunState);
static bool runstate_is_running(void) { return state == RUN_STATE_RUNNING; }
static RunState runstate_get(void) { return state; }
static void bql_unlock(void) { assert(bql); bql = false; }
static void bql_lock(void) { assert(!bql); bql = true; }
static void nv2a_lock_fifo(NV2AState *d) {
    (void)d; assert(bql); assert(!held); held = true;
}
static void nv2a_unlock_fifo(NV2AState *d) {
    (void)d; assert(bql); assert(held); held = false;
}
static void pgraph_pre_savevm_trigger(NV2AState *d) {
    assert(held && d->pfifo.halt); preparations++;
}
static void pgraph_pre_savevm_wait(NV2AState *d) { (void)d; assert(!bql && !held); }
static void pgraph_pre_shutdown_trigger(NV2AState *d) { (void)d; }
static void pgraph_pre_shutdown_wait(NV2AState *d) { (void)d; }
static void global_state_store(void) {}
static int vm_stop(RunState target) {
    if (state == RUN_STATE_RUNNING) {
        state = target; nv2a_vm_state_change(g_nv2a, false, target);
    }
    return 0;
}
static void vm_resume(RunState previous) {
    state = previous;
    if (state == RUN_STATE_RUNNING) nv2a_vm_state_change(g_nv2a, true, state);
    finishes++;
}
static bool migrate_can_snapshot(Error **e) { (void)e; return failure != 1; }
static bool migration_is_blocked(Error **e) { (void)e; return false; }
static bool replay_can_snapshot(void) { return true; }
static void error_setg(Error **e, const char *s, ...) { (void)e; (void)s; }
static bool bdrv_all_can_snapshot(bool a, strList *b, Error **e) { return true; }
static int bdrv_all_delete_snapshot(const char *n, bool a, strList *b, Error **e) { return 0; }
static int bdrv_all_has_snapshot(const char *n, bool a, strList *b, Error **e) { return 0; }
static BlockDriverState *bdrv_all_find_vmstate_bs(const char *n, bool a, strList *b, Error **e) { return &storage; }
static void bdrv_drain_all_begin(void) { assert(drains == 0); drains++; }
static void bdrv_drain_all_end(void) { assert(drains == 1); drains--; }
static GDateTime *g_date_time_new_now_local(void) { return &storage; }
static int64_t g_date_time_to_unix(GDateTime *d) { return 1; }
static int64_t g_date_time_get_microsecond(GDateTime *d) { return 0; }
static char *g_date_time_format(GDateTime *d, const char *f) { return "generated"; }
static int64_t qemu_clock_get_ns(int c) { return 1; }
static uint64_t replay_get_current_icount(void) { return 0; }
static void pstrcpy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }
static QEMUFile *qemu_fopen_bdrv(BlockDriverState *b, int w) { return failure == 2 ? NULL : &storage; }
static uint64_t qemu_file_transferred(QEMUFile *f) { return 16; }
static int qemu_fclose(QEMUFile *f) {
    assert(held && device.pfifo.halt);
    closes++; return failure == 4 ? -1 : 0;
}
static int bdrv_all_create_snapshot(QEMUSnapshotInfo *sn, BlockDriverState *b,
          uint64_t n, bool h, strList *d, Error **e) {
    assert(held && device.pfifo.halt);
    return failure == 5 ? -1 : 0;
}
static int qemu_savevm_state(QEMUFile *f, Error **e);
'''
xen_stubs = r'''
static void global_state_store_running(void) {}
static QIOChannelFile *qio_channel_file_new_path(const char *p, int flags, int mode, Error **e) {
    return failure == 2 ? NULL : &storage;
}
static void qio_channel_set_name(QIOChannelFile *f, const char *n) {}
static QEMUFile *qemu_file_new_output(QIOChannelFile *f) { return f; }
static void object_unref(void *p) {}
static int qemu_save_device_state(QEMUFile *f) { return qemu_savevm_state(f, NULL); }
static void migration_block_inactivate(void) { assert(held && device.pfifo.halt); }
static void vm_start(void) { vm_resume(RUN_STATE_RUNNING); }
'''
writer = r'''
static int qemu_savevm_state(QEMUFile *f, Error **e) {
    /* RAM serialization must not begin before the asynchronous GPU is quiet. */
    assert(preparations == 1 && held && device.pfifo.halt);
    writes++;
    if (failure == 3) return -1; /* Failure before the NV2A section. */
    POST_SAVE
    return failure == 6 ? -1 : 0; /* Failure after that section. */
}
'''.replace('POST_SAVE', 'nv2a_post_save(g_nv2a);' if post else '')
main = r'''
int main(int argc, char **argv) {
    assert(argc == 5);
    RunState initial = atoi(argv[1]) ? RUN_STATE_RUNNING : RUN_STATE_PAUSED;
    failure = atoi(argv[2]);
    bool xen = atoi(argv[3]), initial_halt = atoi(argv[4]);
    if (atoi(argv[3]) == 2) {
        for (int attempt = 0; attempt < 3; attempt++) {
            state = RUN_STATE_RUNNING;
            device.pfifo.halt = initial_halt;
            vm_stop(RUN_STATE_SAVE_VM);
            assert(held && device.pfifo.halt && !device.savevm_explicit);
            nv2a_post_save(g_nv2a);
            assert(!held && !device.savevm_locked && device.pfifo.halt);
            vm_start();
            assert(!held && bql && !device.pfifo.halt);
        }
        return 0;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        state = initial;
        device.pfifo.halt = initial_halt;
        preparations = finishes = writes = closes = 0;
        if (xen) {
            qmp_xen_save_devices_state("test-state", true, true, NULL);
        } else {
            bool ok = save_snapshot("test-snapshot", false, NULL, false, NULL, NULL);
            assert(ok == (failure == 0));
        }
        assert(state == initial && !held && bql && drains == 0);
        assert(device.pfifo.halt == (initial == RUN_STATE_RUNNING && (xen || failure != 1)
                                     ? false : initial_halt));
        assert(!device.savevm_locked && !device.savevm_explicit);
        assert(preparations == (!xen && failure == 1 ? 0 : 1));
        assert(writes == (failure == 1 || failure == 2 ? 0 : 1));
        assert(closes == (failure == 1 || failure == 2 ? 0 : 1));
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='snapshot-lifecycle-') as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    binary = pathlib.Path(tmp) / 'test'
    source.write_text(prelude + helpers + '\n' + handler + '\n' + post + writer + xen_stubs +
                      function(save, 'save_snapshot') +
                      function(save, 'qmp_xen_save_devices_state') + main)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra',
                    '-Wno-unused-function', '-Wno-unused-parameter',
                    str(source), '-o', str(binary)], check=True)
    cases = 0
    for xen in [0, 1]:
        for running in [0, 1]:
            for initial_halt in [0, 1]:
                for fault in ([0, 2, 3, 4, 6] if xen else range(7)):
                    result = subprocess.run([str(binary), str(running), str(fault),
                                             str(xen), str(initial_halt)],
                                            capture_output=True, text=True)
                    if result.returncode:
                        raise SystemExit(f'xen={xen}, running={running}, halt={initial_halt}, '
                                         f'fault={fault}: {result.stderr.strip()}')
                    cases += 3
    for initial_halt in [0, 1]:
        subprocess.run([str(binary), '1', '0', '2', str(initial_halt)], check=True)
        cases += 3
    print(f'PASS: {cases} actual save/NV2A lifecycle attempts (snapshot/Xen, repeated running/paused saves, prior halt, faults)')
