# TCG jump-cache attribution probe

This bounded diagnostic for [research issue #245](https://github.com/Mainkill1/xemu/issues/245)
measures the existing jump-cache behavior before considering lazy invalidation.
It preserves unconditional atomic clears and never dereferences a cached TB to
measure occupancy. It does not establish the correctness or benefit of retaining
entries after invalidation.

## Enable and collect

Build with `-Dxemu_tcg_jump_cache_probe=true` (the default is `false`), then launch
with `XEMU_TCG_JUMP_CACHE_PROBE=1`. Other values leave collection disabled. Query
HMP `info jit` before and after a named workload segment. Counters are cumulative
per vCPU; subtract matching rows to obtain interval counts. Preserve the complete
queries, executable hash, source commit, immutable runner test revision, workload
screenshots and runner performance report.

| Row | Meaning |
| --- | --- |
| `hit` | Dispatch lookup accepted the per-vCPU jump-cache entry. |
| `global_hit` | Dispatch lookup missed the jump cache and found an existing TB in the global table. |
| `global_miss` | Dispatch lookup missed both caches and returned normally. |
| `lookup_started` | Dispatch lookup entered the instrumented path. Exceptions or in-flight calls can leave this above the completed-class total. |
| `pcrel_flush` | Whole-cache clears caused by individual `CF_PCREL` TB invalidation. |
| `other_flush` | Other whole-cache clears; page-selective clears in `cputlb.c` are not counted. |
| `slots` | Slots traversed by the counted whole-cache clears. |
| `observed_nonnull` | Non-null pointer observations before unconditional clears, without TB dereferences. Concurrent writers mean these are not exact unique evictions. |
| `maximum_nonnull` | Largest observed occupancy in one counted clear, cumulative since process start. |
| `occupancy` | Histogram: zero, one, 2–3, 4–7, …, 2048–4095, 4096 or more non-null observations per clear. |
| `targeted_invalidations` / `targeted_removals` | Non-PC-relative individual invalidation attempts and matching jump-cache pointers cleared. |
| `generated` | New translations reaching TB publication, including a possible duplicate later discarded and temporary translations. |
| `recycled` | Existing invalid TBs reused at publication; this is not a retranslation count. |

## Timing and interpretation limits

`calls` counts all completed classified operations. `samples` and `sample_ns`
describe sampled elapsed time. A hash of each event ordinal selects nominally one
lookup in 1024 and one whole-cache clear in 32, avoiding a fixed sampling stride.
Divide interval `sample_ns` by interval `samples` for the sampled mean only when
the latter is nonzero. Do not treat the measured sum as total uninstrumented cost.

Timers include their read overhead. Flush samples also include the probe's extra
per-slot occupancy reads. Counter atomics and occupancy scanning can change cache
pressure and guest cadence. Runtime collection disabled in a diagnostic build
still leaves conditional hook overhead; a normal build excludes the collector.
Diagnostic timings are unsuitable for production speedup claims.

Snapshot fields are independently atomic, not a coherent snapshot of all counters.
CPU activity can continue while HMP formats the rows. Small discrepancies between
related fields are not evidence of lost updates. Differences across two snapshots
also include query timing uncertainty. Histogram differences describe interval
clears, while a difference of cumulative maxima is not an interval maximum.

The hooks count dispatch lookups, individual TB invalidations and whole-cache
clears. They do not attribute a particular miss or new translation to a previous
clear. Page-selective clearing remains outside this probe. Establish workload
reachability, observer cost, and a low-invalidation control before proposing a
cache-retention implementation. Any such candidate still needs explicit stale-TB,
self-modification, lifetime and concurrency correctness checks.

The collector unit test exercises unconditional clearing, preserved PCs, disabled
collection, accounting, sample distribution, formatting and concurrent updates.
The initial current-main profiler and reachability evidence is in
[draft evidence PR #48](https://github.com/Mainkill1/xemu-perf-tests/pull/48).
