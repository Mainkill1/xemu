# TCG jump-cache miss and displacement diagnostics

[Research issue #167](https://github.com/Mainkill1/xemu/issues/167) asks whether
collisions justify a victim cache or a different cache organization. This probe
classifies existing misses and models recently displaced entries. It preserves
production lookups, full translation identity checks and unconditional atomic
clears. No shadow entry is dereferenced, executed or used to fill the real cache.
The branch contains no cache optimization and has demonstrated no speedup.

## Enable and collect

Build with `-Dxemu_tcg_jump_cache_probe=true`; the default is `false`, which
excludes the collector and its call sites. Set `XEMU_TCG_JUMP_CACHE_PROBE` at
launch. Unset, `off`, `0`, and unrecognized values disable collection in an
instrumented build. That build still has conditional hook overhead.

| Mode | Mask | Work collected |
| --- | ---: | --- |
| `counters` | 1 | Lookup, clear, invalidation and translation counts |
| `occupancy` | 3 | Counters and nominally 1/32 clear occupancy |
| `timing` | 5 | Counters and nominally 1/1024 lookup, 1/32 clear timing |
| `all` / `1` | 7 | Counters, sampled occupancy and timing |
| `conflicts` | 9 | Counters, miss reasons and independent eight/sixteen entry FIFO victim models; no clocks or occupancy scan |

Use the maintained runner's HTTP interface. Retain HMP `info jit` queries at
named workload boundaries, source/executable identities, immutable test revision,
renderer, settings, cache-state ledger, screenshots, and all failed attempts.
Counts are cumulative per vCPU. Subtract matching rows for interval counts;
wall-clock windows can include boot/setup and are not guest phase boundaries.

The serialized dispatch owner updates private lookup/model counters. It publishes
atomic copies every 65,536 lookup starts and at execution yields. Live snapshots
can lag by an interval; fields are independently atomic, not a coherent snapshot.
A query may include an entered but unfinished lookup. Shared clear, invalidation
and translation counters still use atomic updates. Only the dispatch owner, or
code after execution quiesces, may publish the final partial interval.

## Read the output

| Field | Meaning |
| --- | --- |
| `hit` | The C lookup accepted the primary jump-cache entry after full PC, segment, flags and cflags validation. |
| `global_hit` | Primary miss; the real global lookup returned an existing, validated translation block. |
| `global_miss` | Both lookups missed and returned normally. |
| `lookup_started` | Entries into the instrumented C lookup; in-flight calls/exceptions can exceed completed totals. |
| `miss_empty` | The primary slot was null. The row separately reports global hits and misses. |
| `miss_pc_conflict` | The primary slot was non-null with a different virtual PC. |
| `miss_translation_state` | The primary PC matched, but full translation identity did not. This includes invalid cflags. |
| `victim8_recovered`, `victim16_recovered` | A validated global hit matched a displaced virtual-PC/TB-token pair in the respective FIFO model before insertion of the newly displaced entry. These are potential recoveries, not time saved. |
| `shadow_resets` | Owner-observed invalidation generations that discarded model history. Several clears can coalesce into one reset. |
| `overlapped_fills` | Publications overlapping invalidation, excluded from recovery accounting and marked unsafe to seed later history. |
| `pcrel_flush`, `other_flush` | Whole-cache clears for individual PC-relative invalidation versus other whole clears. Page-selective clears are outside these totals. |
| `slots` | Slots traversed by those whole clears. |
| `targeted_invalidations`, `targeted_removals` | Non-PC-relative invalidation attempts and matching primary pointers removed. |
| `generated`, `recycled` | New TB publications and invalid-TB reuse; neither directly measures retranslation caused by a clear. |
| `observed_clears`, `observed_slots`, `observed_nonnull` | Sampled occupancy work; divide non-null observations by sampled clears/slots, never by all clears. |
| `maximum_nonnull`, `occupancy` | Largest sampled occupancy (a lower bound) and sampled-clear histogram. A difference of cumulative maxima is not an interval maximum. |
| `samples`, `sample_ns` | Timing sample count and aggregate elapsed nanoseconds; divide by samples only when nonzero. |

Generated fast lookups and direct TB chaining bypass the C lookup. These counts
are **not all guest TB dispatches**. A different-PC slot identifies displacement
pressure, but does not by itself prove that a victim cache would have helped.

## Model ownership and invalidation

Each model removes a matching requested key, then inserts the displaced key at
the FIFO tail, dropping the oldest key at capacity. Primary hits do not touch
these FIFO models. The two capacities evolve independently. New translations
can remove a matching token but cannot count as recoveries. Virtual PC and the
TB returned by the real global lookup must both match; a PC-only match is unsafe.

Whole clears, page/range clears, and targeted invalidations advance a shared
invalidation epoch. Any such invalidation conservatively discards **all** model
history, including entries unrelated to that page/TB. A publication checks the
epoch after the unchanged real primary store and after model work. A per-slot
owner-local taint flag prevents a primary store that overlapped invalidation from
later seeding history. Diagnostic memory includes 4,096 taint flags in addition
to the two model arrays; it is not a proposed production cache memory budget.

This conservative reset can underestimate an implementation with precise
invalidation. Zero recoveries cannot alone reject every cache organization.
The model does not implement the issue's equal-budget 2,048-by-two-way or
larger-budget 8,192-entry direct alternatives. Those need separate experiments
and complete invalidation, remapping, spanning-page, reset/load, reclamation and
concurrency qualification before a production recommendation.

## Observer cost and performance decisions

Conflict mode performs FIFO searches, model moves and memory barriers on primary
fills. It is intentionally a separate diagnostic mode, not low-cost production
instrumentation. Establish same-binary OFF/counters/conflicts observer costs and
compile-disabled versus dormant costs. Do not pool instrumented timing with an
uninstrumented baseline or call recoverability counts a percentage speedup.

The compiled-out path retains the parent's literal primary lookup expression.
Routing it through the observed-lookup wrapper caused GCC 14 O2 to outline
`tb_lookup` in an earlier branch build, while the diagnostic build forced it
inline. A matched dormant comparison then appeared 23.8% faster on stable code.
That comparison mixed compiler code shapes and did not establish a cache gain.
Preserve those results, inspect compiled code, and qualify corrected builds with
fresh identities before interpreting observer overhead or production gains.

Timing includes timer-read overhead and excludes work before/after its timestamps.
Occupancy is genuinely sampled, but still adds reads to sampled clears. Summing
sampled durations cannot reconstruct total observer overhead. Snapshot differences
also include query uncertainty. Use matched uninstrumented builds and fixed-work
A/A, ABBA and BAAB comparisons for any eventual cache candidate; report baseline,
candidate, absolute saving, positive-is-better improvement and correctness.

Maintained unit checks cover full-key lookup rejection, observed displaced keys,
clearing, sampling/accounting, concurrent shared writers, miss classification,
FIFO capacity, invalidation overlap and late-publication token reuse. They do not
prove whole-guest behavior or weak-memory concurrent integration. The collector
is retained from the earlier #245 investigation; its earlier measurements do
not qualify this new classifier or a cache optimization.
