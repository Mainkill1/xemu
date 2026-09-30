# Issue 245: production jump-cache validity helpers

**Status: six bounded unit checks pass; no retention candidate or speedup.**

This follow-up to [draft PR 272](https://github.com/Mainkill1/xemu/pull/272)
extracts existing behavior so unit tests call the same helpers as the emulator.
It preserves unconditional invalidation, complete identity/cflags equality,
atomic accesses, cache shape and the existing mapping/page selection.
It does not implement lazy retention.

## Quick comparison and investment decision

| Requirement | Before this follow-up | After this follow-up | Improvement / decision |
| --- | --- | --- | --- |
| Stale `CF_INVALID` pointer | Existing inline guard; no retained helper regression | Exact production helper rejects deliberately stale slot | Correctness evidence improved; **no measured speedup** |
| Changed PC/base/flags/cflags | Existing complete equality | Same comparisons; changed count, no-chain, single-step, breakpoint, icount, parallel and cluster flags rejected | Preserve complete identity in a future candidate |
| Mapping-related slot ranges | Existing page-range clearing | Same range helper tested, including preceding-page range | Unit boundary check only; real remap/spanning TB tests still required |
| Clearing before object reuse | Existing unconditional clear | Same helper removes cached pointer before same-address identity reuse | Does **not** prove production reclamation ordering |
| Main / fixed baseline vs optimization | No retention candidate | Still none | **Not measured**; no integration recommendation |

**Recommendation:** retain existing clearing. This closes a helper regression gap
needed for a candidate; whole production invalidation, remapping, storage lifetime,
reset/load, concurrency and matched useful-work performance remain gates.

## Processing flow / code

- [`cpu-exec.c`](../../../accel/tcg/cpu-exec.c) calls `tcg_jump_cache_lookup()`
  from [`tb-jmp-cache.h`](../../../accel/tcg/tb-jmp-cache.h). The former inline
  comparison is preserved, including `tb_cflags(tb) == s.cflags` and the existing
  likely hint. `CF_INVALID` is never masked from cache-hit validation.
- [`cputlb.c`](../../../accel/tcg/cputlb.c) keeps its existing page hash and
  early-null guard, and calls `tcg_jump_cache_clear_range()` for that range.
  The caller still selects the remapped page and preceding page for spanning TBs.
- [`translate-all.c`](../../../accel/tcg/translate-all.c) calls the same range
  helper for a normal full jump-cache clear. Enabled diagnostic full clears still
  use their existing collector implementation, whose four tests remain separate.
- [`test-tcg-jump-cache-validity.c`](../../../tests/unit/test-tcg-jump-cache-validity.c)
  uses real `TranslationBlock`, `CPUJumpCache`, hash functions and the production
  helpers with the existing i386 target-header Meson dependency.

## Tests and limits

| Unit case | What the test checks | What remains unproved |
| --- | --- | --- |
| Invalid entry | An entry with published `CF_INVALID` cannot match; the stale pointer deliberately remains cached | Actual `do_tb_phys_invalidate()` publication/chaining sequence |
| Complete identity | Every required state field and compile-flag change rejects the old entry | Live debugger and guest instruction execution |
| Same-slot replacement | A valid non-PC-relative replacement works while the invalid former entry misses | Concurrent replacement/publication |
| Page range | Selected slots clear; unrelated slots and stored PCs remain | Physical self-modification and real virtual MMU remap |
| Spanning-page ranges | Clearing the preceding page removes a TB-start slot; first-page range also clears it | This constructs a slot, not a real spanning translated guest TB |
| Same-address reuse | After full range clearing, reusing a TB object at the same address does not expose the old cached pointer | Real code-storage reclamation, exclusive ordering, reset/load and multi-vCPU races |

Native tests pass **6/6**; collector tests pass **4/4**. ASan/UBSan pass **6/6**
with leak detection and halt-on-error enabled. Two independent negative controls
are rejected: masking `CF_INVALID` from equality, and suppressing clearing before
same-address reuse. The implementation is restored after each control.
A missing-helper build fails before extraction; setup failures are retained
separately rather than counted as negative correctness controls.

Full native default-OFF and Windows probe-enabled emulator builds both link.
The final native build and final Windows rebuild have no warning/error lines.
The preceding Windows full build retains warnings in unchanged imgui, toml++,
block graph, Vulkan allocator and Vulkan debug headers; it is not warning-free.
The Windows test executable was built, **not run**. The active unrelated Windows
Def Jam test was not stopped or replaced.

To reproduce the normal unit checks in a configured native build directory:

```sh
ninja tests/unit/test-tcg-jump-cache-validity tests/unit/test-tcg-jump-cache-probe
./tests/unit/test-tcg-jump-cache-validity
./tests/unit/test-tcg-jump-cache-probe
```

The receipt retains the exact executed commands, compiler flags, configuration
and exit codes. The alternate probe-enabled native slot layout also passes six
checks using a compile shim; this is not a full probe-enabled native rebuild.

## Build / source / performance scope

Measured PGR2/CPU diagnostic executables remain the earlier `06168a2f` source,
with identities in the [PGR2 report](../research245-probe-20260930/REPORT.md)
and [generated-code report](../research245-cpu-probe-20260930/REPORT.md).
**This follow-up changes source after those measurements.** It makes no claim
that the earlier runs execute these helpers, and no new emulator benchmark is
reported for this follow-up. The full behavioral candidate does not exist yet.

The local source HEAD at test preparation is
`33508c693dab0638c506d50434949ea700d94c97` plus the retained patch/source hashes.
The local version build context uses that exact source commit without malformed
local synthetic version tags; no product version behavior was changed.
The test receipt pins changed source bytes and executable hashes. No binary
code-size comparison is used as performance evidence.

## Morrowind input preparation

The unchanged full VM-state carrier was added to the Deck catalog as
`research245-morrowind-carrier-d62e336a`, SHA-256
`d62e336ae43e9790954604a46c1236c4884e952bf1aace9a56106bed631891c3`.
The runner confirms `vm-20260929162035` contains 67,363,427 VM-state bytes.
This proves input identity and presence, **not snapshot compatibility**.

Read-only asset preparation found no disc at the retained Deck path. No Morrowind
run was launched or claimed passed. The observed retained Windows configuration
also differs from its saved template's pinned config hash; it is retained only
as a preparation reference. A future test must use freshly pinned immutable
inputs, not treat that observed file as certified historical bytes.
Morrowind scene admission, counters and matched performance remain outstanding.

## Evidence and verification

[`receipt.json`](receipt.json) records source/build/test scope.
[`SHA256SUMS`](SHA256SUMS) pins the complete public folder. Run `python3 audit.py`
in this folder to check the inventory, source bytes, test counts and controls.
Logs retain earlier setup failures, negative controls and final checks.
Preparation receipts are in `morrowind-preparation/`; guest/media binaries,
EEPROM and executable binaries are excluded. Source-adjacent original project
code was extracted independently of any foreign implementation.
