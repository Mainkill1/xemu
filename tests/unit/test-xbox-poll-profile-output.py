#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile actual recording paths; steady-state profiling must not emit."""
import pathlib
import re
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "util/qemu-timer.c").read_text()
def extract(name):
    match = re.search(r"static void " + name + r"\([^)]*\)\s*\{", source)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]
preamble = r"""
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned guint;
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define NANOSECONDS_PER_SECOND 1000000000LL
typedef struct {
 uint64_t entries,all_calls,timeout_buckets[6],requested_ns,spin_ns,iterations;
 uint64_t ready_exits,errors,late_ns,max_late_ns,max_fds,last_report_ns;
 uint64_t negative_calls,zero_calls,long_calls,nonspin_ready_exits,nonspin_errors;
} XboxPollSpinProfile;
static XboxPollSpinProfile xbox_poll_spin_profile;
static int enabled = 1, emissions;
static int xbox_poll_spin_profile_enabled(XboxPollSpinProfile *p)
{ return enabled; }
static void xbox_poll_profile_record_context(XboxPollSpinProfile *p,
                                             int64_t t, int64_t s) {}
static void xbox_poll_profile_emit(XboxPollSpinProfile *p, const char *reason)
{ emissions++; }
"""
main = r"""
int main(void) {
 for (int i=0;i<20000;i++) xbox_poll_profile_record_nonspin(i%2 ? 0 : 500000,0);
 assert(xbox_poll_spin_profile.all_calls==20000);
 assert(xbox_poll_spin_profile.zero_calls==10000);
 assert(emissions==0);
 for (int i=0;i<20000;i++) {
     xbox_poll_spin_profile_record(500000,500000,100,0,1,0);
 }
 assert(xbox_poll_spin_profile.entries==20000);
 assert(xbox_poll_spin_profile.all_calls==40000);
 assert(emissions==0);
 enabled=0; xbox_poll_profile_record_nonspin(0,0);
 assert(xbox_poll_spin_profile.all_calls==40000);
 puts("PASS: counters advance without synchronous steady-state output");
}
"""
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / "test.c"
    exe = pathlib.Path(tmp) / "test"
    c.write_text(preamble + extract("xbox_poll_spin_profile_record")
                 + extract("xbox_poll_profile_record_nonspin") + main)
    subprocess.run(["cc", "-fsanitize=address,undefined", "-g", str(c),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)],check=True)
