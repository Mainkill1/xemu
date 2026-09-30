# TCG jump-cache attribution probe

This bounded diagnostic for [research issue #245](https://github.com/Mainkill1/xemu/issues/245)
measures the existing jump-cache behavior before considering lazy invalidation.
It preserves unconditional atomic clears and never dereferences a cached TB to
measure occupancy. It does not establish the correctness or benefit of retaining
entries after invalidation.

## Enable and collect

Build with `-Dxemu_tcg_jump_cache_probe=true` (the default is `false`), then launch
with `XEMU_TCG_JUMP_CACHE_PROBE` set to a mode below. Unset, `off`, `0` and
unrecognized values leave collection disabled. Query HMP `info jit` before and
after a named workload segment. Counters are cumulative
per vCPU; subtract matching rows to obtain interval counts. Preserve the complete
queries, executable hash, source commit, immutable runner test revision, workload
screenshots and runner performance report.

| Mode | Published mask | Collection |
| --- | --- | --- |
| `counters` | 1 | Exact dispatch/clear counts; no clocks or occupancy reads |
| `occupancy` | 3 | Counters plus occupancy on nominally 1/32 clears; no clocks |
| `timing` | 5 | Counters plus nominal 1/1024 lookup and 1/32 clear timing; no occupancy reads |
| `all` / `1` | 7 | Counters, sampled occupancy and sampled timing |

Each vCPU's serialized dispatch thread maintains private lookup totals. The
lookup hooks are inlined and update each private total once. They publish an
atomic copy every 65,536 lookup starts and at normal/atomic execution yields.
The publication at a lookup boundary includes that start, but not its eventual
completion. Live snapshots may lag by one publication interval, and their atomic
fields are independent. A quiescent owner publication retains the final partial
interval. Shared invalidation, clear and code-generation writers still use
atomic read-modify-write accounting; live readers never read private owner state.
A 64-byte gap keeps the hot owner fields away from shared counter cache lines
without assuming that the allocator returns a 64-byte-aligned address.

| Row | Meaning |
| --- | --- |
| `hit` | Dispatch lookup accepted the per-vCPU jump-cache entry. |
| `global_hit` | Dispatch lookup missed the jump cache and found an existing TB in the global table. |
| `global_miss` | Dispatch lookup missed both caches and returned normally. |
| `lookup_started` | Dispatch lookup entered the instrumented path. Exceptions or in-flight calls can leave this above the completed-class total. |
| `pcrel_flush` | Whole-cache clears caused by individual `CF_PCREL` TB invalidation. |
| `other_flush` | Other whole-cache clears; page-selective clears in `cputlb.c` are not counted. |
| `slots` | Slots traversed by the counted whole-cache clears. |
| `observed_clears` / `observed_slots` | Sampled clears / slots actually scanned for occupancy; zero in counters and timing modes. |
| `observed_nonnull` | Non-null observations in **sampled** clears, without TB dereferences. Divide by `observed_clears` for sampled mean occupancy, or by `observed_slots` for sampled non-null fraction. Do not divide by all clear calls/slots. |
| `maximum_nonnull` | Largest occupancy in a **sampled** clear; lower bound on the process maximum, not an exact maximum. |
| `occupancy` | Sampled-clear histogram: zero, one, 2–3, 4–7, …, 2048–4095, 4096 or more observations. Sum equals `observed_clears` once quiescent. |
| `targeted_invalidations` / `targeted_removals` | Non-PC-relative individual invalidation attempts and matching jump-cache pointers cleared. |
| `generated` | New translations reaching TB publication, including a possible duplicate later discarded and temporary translations. |
| `recycled` | Existing invalid TBs reused at publication; this is not a retranslation count. |

## Timing and interpretation limits

`calls` counts all completed classified operations. `samples` and `sample_ns`
describe sampled elapsed time. Timing mode uses an owner-local xorshift decision
for nominally one lookup in 1024. A mixed clear ordinal independently selects
nominally one clear in 32 for occupancy/timing modes. Counter-only mode skips
both sampling decisions. Samples avoid a fixed stride; deterministic distribution
checks do not prove independence from every possible workload.
Divide interval `sample_ns` by interval `samples` for the sampled mean only when
the latter is nonzero. Do not treat the measured sum as total uninstrumented cost.

Timers include read overhead; sequence/sampling decisions before the first clock
and publication after the second clock are excluded. Flush timings include extra
occupancy reads only in `all` mode. Never sum sampled durations to estimate total
observer cost. Periodic publication, shared counters and sampled occupancy scanning
can change cache pressure and guest cadence. Collection disabled in a diagnostic build
still leaves conditional hook overhead; a normal build excludes the collector.
Diagnostic timings are unsuitable for production speedup claims.

Snapshot fields are independently atomic, not a coherent snapshot of all counters.
CPU activity can continue while HMP formats the rows. Lookup totals refer to
the last owner publication, not the precise monitor-query instant. Small discrepancies between
related fields are not evidence of lost updates. Differences across two snapshots
also include query timing uncertainty. Histogram differences describe sampled
interval clears, while a difference of cumulative maxima is not an interval
maximum. Earlier evidence used **every-clear** occupancy and a heavier lookup
collector; its numeric definitions must not be applied to this revised collector.

The hooks count dispatch lookups, individual TB invalidations and whole-cache
clears. They do not attribute a particular miss or new translation to a previous
clear. Page-selective clearing remains outside this probe. Establish workload
reachability, observer cost, and a low-invalidation control before proposing a
cache-retention implementation. Any such candidate still needs explicit stale-TB,
self-modification, lifetime and concurrency correctness checks.

The collector unit test exercises unconditional clearing, preserved PCs, disabled
collection, accounting, sample distribution, formatting and concurrent updates.
The initial current-main profiler and reachability evidence is in
[product evidence report](../evidence/research245-20260930/REPORT.md).
The [probe measurements](../evidence/research245-probe-20260930/REPORT.md)
and [CPU fixture qualification](../evidence/research245-cpu-fixtures-20260930/REPORT.md)
also belong to this product draft. No lazy-invalidation speedup has been demonstrated.

Generated-code [probe windows and retained CPU outcomes](../evidence/research245-cpu-probe-20260930/REPORT.md)
include the lower-invalidation stable control and the self-modifying workload.
These wall-clock diagnostic windows are not exact guest phase boundaries.

The [production validity helper checks](../evidence/research245-validity-20260930/REPORT.md)
verify complete identity rejection and slot-clearing boundaries. They do not
prove whole guest remapping, reclamation, reset/load or concurrent invalidation.

The [lighter collector checks and host comparisons](../evidence/research245-light-probe-20260930/REPORT.md)
retain all mode costs, sanitizer checks and deliberate negative controls.
[Same-executable Deck/Windows CPU mode checks](../evidence/research245-light-probe-guests-20260930/REPORT.md)
report actual per-leaf work times and preserve every cache-qualification rejection.
These measurements assess observer cost; they do not test cache retention.
