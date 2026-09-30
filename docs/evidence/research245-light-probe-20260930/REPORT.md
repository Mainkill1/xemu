# Issue 245: reduce and isolate observer cost

## Recommendation: HOLD

**Summary:** A lighter collector reduces cost in a matched host microbenchmark.
Every cache clear write remains. This is a collector improvement, not a retention
optimization or an emulator speedup.
**Remaining:** Qualified same-executable guest mode comparisons, matched parent/compiled-OFF
builds and updated main timing context, cache qualification, remapping/lifetime/reset/concurrency production proof,
Morrowind, and an actual retention candidate.

## Quick comparison

Process CPU time; matched compiler/options and fixed operation counts; median of
four observations per setting in ABBA/BAAB order. Positive Improvement is better.

| Host-only workload | Earlier collector | Lighter collector | Time difference | Improvement % | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| 5M lookup begin/end calls, collection all | 47.431 ms | 10.751 ms | -36.680 ms | +77.33% | Less observer cost in this synthetic loop |
| 50K clears × 4,096 empty slots, collection all | 60.667 ms | 43.219 ms | -17.448 ms | +28.76% | Sample occupancy instead of reading every clear |
| 5M NULL-probe lookup guards | 8.350 ms | 9.390 ms | +1.040 ms | **-12.46%** | Retain regression; this is not an emulator OFF comparison |
| Actual cache retention vs production reference | Not tested | No candidate exists | Not measured | **NOT COMPARABLE** | No integration recommendation |

The NULL-probe benchmark **calls** begin/end; real dispatch checks the probe
pointer before calling them. It therefore does not establish a regression in
the emulator's collection-OFF path. The clear benchmark uses empty slots and
still performs every store. Neither benchmark executes guest instructions,
graphics, invalidation, remapping or real code reclamation.

One excluded host warmup precedes each setting/comparison. All 80 retained runs
pass exact operation-count checks; the 20 warmups are also retained. A separate
identical-executable OFF A/A has apparent differences -0.05% for lookup guards
and -0.40% for clear calls. Those are controls, not improvements. CPU affinity
is pinned; frequency, thermal state and other host activity are not locked.
These few repetitions do not establish universal overhead percentages.

## Why / processing flow

The earlier probe performs two atomic read-modify-writes and a mixed ordinal
decision on every lookup. It reads every slot on every collected clear; 1/32
sampling controls only the clocks. The reported durations exclude sequence/hash
work before the first clock and accounting after the last clock, so summing them
cannot recover total observer cost. The earlier CPU comparisons used unmatched
builds and cannot attribute their slower results to a specific code change.

```text
BEFORE lookup: atomic sequence RMW → mixed decision → optional clock
               → existing lookup → optional clock → atomic class RMW
AFTER lookup:  owner sequence → atomic value publication
               → clocks/cheap owner sampler only in timing modes
               → existing lookup → owner class total → atomic publication

BEFORE clear:  count clear → read and write all 4,096 slots
               → exact every-clear occupancy + sampled duration
AFTER clear:   count clear → if occupancy sample: read + write all slots
                            otherwise: write all slots
               → explicit sampled occupancy denominator / optional duration
```

All three `tb_lookup()` call paths run on the owning vCPU's serialized dispatch
thread, including exclusive atomic stepping. Private lookup totals/RNG are never
read by another thread. Readers use atomic published values; these values do
not form a coherent snapshot. Shared clear, targeted-invalidation and codegen
writers retain atomic RMW updates. Cache allocation/RCU lifetime is unchanged.
Do not share one probe between parallel lookup dispatchers.

## Modes / meaning

| Runtime mode | Lookup/clear counts | Occupancy | Timing |
| --- | --- | --- | --- |
| `off` / `0` / unset / unrecognized | Disabled | None | None |
| `counters` | Exact cumulative counts | None | No clocks or sampling decisions |
| `occupancy` | Same counts | Nominal 1/32 clears | None |
| `timing` | Same counts | None | Nominal 1/1,024 lookups and 1/32 clears |
| `all` / `1` | Same counts | Nominal 1/32 clears | Same timing selection |

`observed_clears` counts occupancy samples; `observed_slots` counts scanned slots.
Divide observed non-null pointers by **observed_clears**, not all clear calls,
for sampled mean occupancy. Histogram sum equals sampled clears when quiescent;
maximum is a sampled lower bound. Timing-only clears exclude occupancy reads.
The 50K-clear benchmark scans occupancy on 1,616 new clears versus all 50,000
old clears. This is a logical read-count reduction, not external memory bandwidth.
Earlier reports retain their earlier every-clear definitions.

## Correctness / build checks

| Check | Result / limit |
| --- | --- |
| Collector cases | 7/7 pass: preserved writes/PCs; disabled; sampled occupancy; requested mode; owner/reader; accounting/distribution; shared invalidation/clear writers |
| Validity helpers | 6/6 pass in native enabled layout; production guest/lifetime tests remain missing |
| ASan/UBSan | 7/7 pass with leak detection, abort/halt on error |
| ThreadSanitizer | 7/7 pass, including concurrent reader and shared clear writers; no multi-vCPU guest proof |
| Occupancy regression | Old collector fails new sample-count bound: 1,024 observations instead of fewer than 80 |
| Counter-mode negative control | Deliberately enabling timing in counters mode fails requested-mode assertion; restored implementation passes |
| Initial negative-control attempt | Incorrectly passed because test derived expectations from reported bits; retained, then corrected to assert requested mode |
| Native enabled / Windows enabled builds | Both link at exact source commit; Windows unit built, not executed |
| Native default OFF | Recorded separately in receipt; no broader CI/full-XISO claim |

Earlier full native builds retain unrelated graphics warnings; preceding Windows
builds retain third-party/header warnings. Changed collector/dispatch/unit files
introduce no warning lines. Initial standalone benchmark link/include setup
failures are retained and do not count as correctness controls.

## Source and evidence applicability

Product source: `3a3d3c390fbb5d3835206d47614a86467e7a9330`; reference collector:
`06168a2f455b832bc6eb7936ebe725ec530b1b00`. The host benchmark compiles both
collectors with the same native flags and real QEMU timer initialization, without
LTO; reference/new header definitions accompany their respective source.
[All ten comparisons](host/COMPARISON.md), benchmark source, exact commands, hashes and rows are retained.
Both harness executables were later reproduced byte-identically using the current
default-OFF generated configuration; original generated headers were not archived.
Its original build-directory paths describe the executed environment.

Native/Windows emulator builds use the exact product source commit with a tag-free
version context to avoid malformed local synthetic version tags; product version
logic is unchanged. The deployed Deck executable is SHA-256
`6887481a8e22e008259f1cbff86e70f538b56ac7680258a9e68f8397216be3f7`.
The host executable is a standalone collector harness, not this emulator binary.

Earlier PGR2/generated-code timings remain measurements of `06168a2f`, not this
source. Full-suite failures remain **previous-main** missing references/output
mismatches, not regressions caused by this collector or nonexistent retention.

## Guest comparison scope

Both Deck CPU leaves use the same executable, pinned fixture ISO/catalog,
dependencies, firmware/private seed, Vulkan backend, warmups=0 and multiplier=1.
Two OFF A/A runs precede a symmetric sweep of OFF, counters, occupancy, timing,
all, all, timing, occupancy, counters, OFF. The [Deck packet](../research245-light-probe-guests-20260930/deck/REPORT.md) retains **24 leaf attempts**, all correctness PASS and evidence complete; the [Windows packet](../research245-light-probe-guests-20260930/windows/REPORT.md) retains another **24 completed attempts**, also correctness PASS and evidence complete.
The procedures remove HMP queries; per-leaf guest work times come from the
runner's normalized results. This does not introduce exact guest counter windows.

[The guest summary](../research245-light-probe-guests-20260930/REPORT.md) retains actual per-leaf times, raw samples and every canonical outcome. Driver namespace
qualification remains false; no waiver is enabled. These observer runs cannot
establish an accepted production Improvement. Windows became available after its previous investigation completed; its comparison runs use the maintained HTTP client.

## Recheck

Run `python3 audit.py` here for full SHA-256 inventory, historical source hashes,
host run counts/derived comparisons, A/A rows and actual unit logs. Raw guest
media, firmware/EEPROM and compiled binaries are excluded from this public folder.

Main advanced to `a05db7b2375d9c25fe61b6c914e83bab57fc49ca` after the tested
source, incorporating NV2A virtual-time changes. The guest packet therefore binds
these runs to `3a3d3c39`; they cannot qualify a rebased whole emulator automatically.

The branch was rebased onto main `a05db7b2375d9c25fe61b6c914e83bab57fc49ca`
as `e5afa40f6deb715c60beecba68ab4dcf463c85bb`. Collector/dispatch/helper/unit sources and QEMU atomic/timer
dependencies are byte-identical to the measured revision; the full emulator
includes the upstream NV2A timing changes. Current whole-emulator mode comparisons
remain a separate missing gate.
