# Lookup-probe observer cost: owner-local correction

## Recommendation: HOLD

The correction removes per-lookup atomic publication and inlines the dispatch hooks. It improves the standalone tight cache-hit loop versus the prior collector, but does not establish the issue’s under-5% overhead target. No cache-retention candidate exists. Guest qualification and production safety gates are separate.

## What the external review actually matches

| Review finding | Verified current source / action |
| --- | --- |
| Redundant per-lookup atomic publication | Confirmed. Private sequence and classified counts now publish every 65,536 starts and on normal/atomic CPU execution yields. |
| No concurrent reader / make everything local | Live HMP `info jit` readers exist. Snapshots still read atomic published fields; shared clear/targeted/codegen writers retain atomic operations. |
| `summary` mode, budget increment, latency divided by all lookups | These do not exist in the reviewed product revision. Modes are OFF/counters/occupancy/timing/all; reports expose raw calls, samples and sampled duration separately. |
| Raw ticks mislabeled nanoseconds | Current `get_clock()` returns nanoseconds: CLOCK_MONOTONIC on Linux; calibrated QueryPerformanceCounter on Windows. |
| Floor division of partial clear spans | Probe receives explicit slot count and preserves all clearing writes. Page-selective clearing uses the existing separate helper. |
| Exit-only CPU registry | No such registry exists in this implementation. Probe lifetime follows the RCU-protected per-CPU jump cache. |
| Dormant controls and inline hooks | Valid measurement gaps. Inline hooks implemented; compiled-out/OFF/counters/timing/all full-emulator matrix is being measured. |

## BEFORE / AFTER

```text
BEFORE lookup: out-of-line begin → private increment + atomic store → validation
               → out-of-line end → private class increment + atomic store
AFTER lookup:  inline private sequence → every 65536 starts? publish : continue
               → validation → inline private class increment
               → normal/atomic execution yield: publish trailing counts
Timing mode:   sampled nanosecond clocks; calls/samples/duration stay private
Shared clear:  sampled occupancy + atomic shared accounting; all slot writes remain
```

Live fields are individually atomic, not a coherent multi-field transaction. A periodic publication occurs before that lookup completes; started and completed totals may differ by one at the boundary. Readers may lag by one publication interval while a CPU remains executing; execution yield publishes the tail. The owner and shared clear state have a 64-byte separation gap without imposing unsupported malloc alignment.

## Standalone matched-harness comparisons

| Workload | Before → after | Before CPU time | After CPU time | Delta | Improvement % | Paired median 95% CI | p95 / worst slowdown |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Tight cache hit | compile-disabled → enabled runtime OFF | 3.092989 ms | 3.092769 ms | -0.000219 ms | **+0.01%** | [-0.12, +0.19]% | +0.79% / +10.70% |
| Tight cache hit | enabled runtime OFF → owner sequence only (ablation) | 3.101389 ms | 3.104074 ms | +0.002685 ms | **-0.09%** | [-0.35, +0.08]% | +33.09% / +33.57% |
| Tight cache hit | owner sequence only (ablation) → owner sequence + classified call count without publication (ablation) | 3.115549 ms | 5.500061 ms | +2.384512 ms | **-76.54%** | [-81.16, -65.70]% | +101.08% / +106.12% |
| Tight cache hit | owner sequence + classified call count without publication (ablation) → candidate counters incl periodic publication | 5.416410 ms | 6.215878 ms | +0.799467 ms | **-14.76%** | [-20.83, -13.39]% | +32.15% / +36.12% |
| Tight cache hit | candidate counters incl periodic publication → candidate timing incl periodic publication | 6.208743 ms | 6.479675 ms | +0.270933 ms | **-4.36%** | [-4.63, -3.59]% | +4.89% / +5.07% |
| Tight cache hit | f105 counters incl per-lookup publication/out-of-line helpers → candidate counters incl periodic publication | 13.576161 ms | 6.220308 ms | -7.355854 ms | **+54.18%** | [+53.65, +54.26]% | -49.83% / -49.68% |
| Tight cache hit | f105 runtime OFF → enabled runtime OFF | 3.093209 ms | 3.092349 ms | -0.000860 ms | **+0.03%** | [-0.11, +0.15]% | +0.85% / +7.80% |
| Tight cache hit | compile-disabled → candidate counters incl periodic publication | 3.095274 ms | 6.202388 ms | +3.107114 ms | **-100.38%** | [-102.35, -99.74]% | +133.44% / +136.01% |
| Cache hit + 16 dependent integer rounds | compile-disabled → enabled runtime OFF | 98.308438 ms | 98.302663 ms | -0.005775 ms | **+0.01%** | [+0.01, +0.03]% | +0.07% / +0.09% |
| Cache hit + 16 dependent integer rounds | enabled runtime OFF → owner sequence only (ablation) | 98.364908 ms | 98.387008 ms | +0.022100 ms | **-0.02%** | [-0.07, -0.02]% | +1.09% / +2.60% |
| Cache hit + 16 dependent integer rounds | owner sequence only (ablation) → owner sequence + classified call count without publication (ablation) | 98.342393 ms | 98.357769 ms | +0.015376 ms | **-0.02%** | [-0.04, -0.00]% | +0.06% / +0.06% |
| Cache hit + 16 dependent integer rounds | owner sequence + classified call count without publication (ablation) → candidate counters incl periodic publication | 98.354188 ms | 98.358032 ms | +0.003844 ms | **-0.00%** | [-0.03, +0.01]% | +0.10% / +0.12% |
| Cache hit + 16 dependent integer rounds | candidate counters incl periodic publication → candidate timing incl periodic publication | 98.316137 ms | 98.483810 ms | +0.167673 ms | **-0.17%** | [-0.18, -0.15]% | +0.28% / +0.45% |
| Cache hit + 16 dependent integer rounds | f105 counters incl per-lookup publication/out-of-line helpers → candidate counters incl periodic publication | 98.446984 ms | 98.359378 ms | -0.087606 ms | **+0.09%** | [+0.08, +0.12]% | -0.04% / +0.21% |
| Cache hit + 16 dependent integer rounds | f105 runtime OFF → enabled runtime OFF | 98.312633 ms | 98.296782 ms | -0.015851 ms | **+0.02%** | [-0.02, +0.03]% | +0.32% / +0.69% |
| Cache hit + 16 dependent integer rounds | compile-disabled → candidate counters incl periodic publication | 98.282297 ms | 98.307397 ms | +0.025100 ms | **-0.03%** | [-0.04, -0.00]% | +0.08% / +0.10% |

Positive Improvement means less process CPU time. Main statistic: difference of setting medians. Confidence interval: percentile bootstrap of the median paired Improvement, 10,000 resamples with recorded seed; it estimates within-session variation only. p95/worst are paired slowdown observations, including unfavorable outliers. Different comparisons have independent runs, so their costs do not sum exactly. All 960 retained observations plus 32 excluded warmups remain in [raw runs](host/ablation-runs.json).

Each observation performs five million actual `tcg_jump_cache_lookup()` hit validations with full PC/base/flags/cflags checks. A compiler memory barrier prevents hoisting as a generated TB can clobber memory; a deterministic integer checksum prevents dead-code elimination and matches every pair. The sixteen-round case is an artificial cost ablation, **not a realistic game, generated-code dispatch, `mov-gpr` test, or emulator speedup claim**. Sequence-only and unpublished-count stages are diagnostic harness variants, not shipping modes. Final snapshot publication occurs outside the timed region.

The inherited allowed CPUs were 0–31; benchmark process was pinned to CPU31. Frequency policy was recorded (`amd-pstate-epp`, governor `powersave`, EPP `performance`, min 599MHz/max 5185MHz) and was not locked. Timing uses CLOCK_PROCESS_CPUTIME_ID; wall timing also retained. Thirty randomized AB/BA pairs per comparison, one excluded warmup/setting, identical compiler flags and timer implementation. PMU instructions/cycles/branches/misses were requested but [denied by perf permissions](host/pmu.log), not estimated.

This run measured source `aa8bb9ab` versus the exact prior collector `f105bdc7`. The rebased code `0bd18bf2` has [byte-identical collector/dispatch/helper/atomic/timer inputs](host/rebase-equivalence.json). This equivalence applies to the standalone experiment; full-emulator observations identify their own source and executable. [Compiler commands, binary/source hashes and policies](host/ablation-receipt.json), [all arithmetic](host/ablation-summary.json), [harness](host/ablation.c), [measurement script](host/measure.py).

## Correctness and build checks

Nine collector tests and six production-helper checks pass. Nine collector cases also pass ASan/UBSan and TSan, including live owner/reader publication and multiple shared clear writers. The deferred-publication check first failed against the old implementation (published 7 instead of 0), then passed after the correction. No test claims production remapping/storage-reclamation/retention safety. Raw red, green, sanitizer and unit logs are in [host evidence](host/).

Matched native and Windows compile-enabled/disabled build receipts pin exact binaries; modified source files have no warning lines. Existing unrelated GL/Vulkan shadow and Windows allocator/format warnings remain in build logs. Windows unit executables were built previously; they are not counted as executed checks here.

## Guest matrix

All 88 executed guest attempts completed with correctness PASS and complete canonical evidence. **48 clean attempts are retained; 40 completed attempts are excluded** with source/ordering/intervention reasons. Every comparison is cache-unqualified; no waiver. One additional Windows attempt failed preflight before execution, leaving its remaining rewrite modes and clean parent controls missing.

| Host / test ID (Vulkan) | Reference / guest median | Candidate / guest median | Difference | Improvement % | Correctness / qualification |
| --- | ---: | ---: | ---: | ---: | --- |
| deck / `cpu_translation_blocks.code_stable` | disabled 2.587425 s | off 3.422808 s | +0.835383 s | **-32.29%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_stable` | off 3.437851 s | counters 3.423826 s | -0.014026 s | **+0.41%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_stable` | off 3.437851 s | timing 3.427633 s | -0.010218 s | **+0.30%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_stable` | off 3.437851 s | all 3.444631 s | +0.006779 s | **-0.20%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_stable` | parent 2.591588 s | disabled 2.575938 s | -0.015649 s | **+0.60%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_rewrite` | disabled 3.032210 s | off 2.971599 s | -0.060611 s | **+2.00%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_rewrite` | off 2.971599 s | counters 3.047435 s | +0.075837 s | **-2.55%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_rewrite` | off 2.971599 s | timing 3.090297 s | +0.118698 s | **-3.99%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_rewrite` | off 2.971599 s | all 3.258400 s | +0.286801 s | **-9.65%** | PASS / ineligible |
| deck / `cpu_translation_blocks.code_rewrite` | parent 3.039094 s | disabled 2.978347 s | -0.060748 s | **+2.00%** | PASS / ineligible |
| windows / `cpu_translation_blocks.code_stable` | disabled 1.080810 s | off 1.105094 s | +0.024284 s | **-2.25%** | PASS / ineligible |
| windows / `cpu_translation_blocks.code_stable` | off 1.105094 s | counters 1.382116 s | +0.277022 s | **-25.07%** | PASS / ineligible |
| windows / `cpu_translation_blocks.code_stable` | off 1.105094 s | timing 1.405775 s | +0.300681 s | **-27.21%** | PASS / ineligible |
| windows / `cpu_translation_blocks.code_stable` | off 1.105094 s | all 1.452083 s | +0.346989 s | **-31.40%** | PASS / ineligible |

`parent` = exact upstream `458730bf`; `disabled` = candidate compiled without the probe; `off` = enabled binary with collection OFF. Counters/timing/all comparisons use that same enabled executable. Guest fixed-work times are reported in seconds; they are not host CPU cost or FPS. Stable-code performs 50M operations/sample, rewrite 1M. Statistic: median of two independent process-attempt guest medians, ten samples/attempt. Positive Improvement means less time. Small differences remain descriptive given A/A variation, unlocked power/thermal state and cache rejection.

The host standalone improvement does **not** establish the full-emulator overhead target: Windows stable counters measured −25.07%, timing −27.21%, all −31.40% versus OFF. These observations remain visible even though unqualified. Matched-parent controls are complete only on Deck. No current game/OpenGL/unaffected-graphics or host-CPU-per-guest-work qualification was performed. Whole-run metrics, collector duty and memory are archived but lack guest boundaries and thread-specific utilization. No game average/p95/p99 frame-time claim is made.

This packet’s guest source is **0bd18bf26b894852f934a231a29a09b2f5eb480c**, based on **458730bf53**. [Deck: all 10 comparisons, two A/A pairs,54 executed attempts and exclusions](deck/REPORT.md); [Windows: all 4 stable comparisons, two A/A pairs,34 executed attempts and storage rejection](windows/REPORT.md). Native enabled/disabled executable SHA256 and Windows enabled/disabled identities are in [Deck build receipt](host/deck-build-receipt.json) and [Windows build receipt](host/windows-build-receipt.json); [matched parent configuration](host/parent-configuration-equivalence.json) and original receipts are retained. Generated harness configuration rebuilt all 8 measured binaries byte-identically.

## Campaign failures and remaining gates

An upstream advance required fresh 0bd binaries. Earlier source 4 Deck/6 Windows runs are excluded. The followup launcher mistakenly treated an `attention`, terminal=false response for a prepared campaign as readiness to run controls. Two parent-prelude attempts perhost and all 24 original rewrite attempts are excluded from clean arithmetic. Those original sweeps also record bulk transfers while fresh plans were staged. The Deck dormant compiled-out/OFF pair was replaced with four clean ABBA runs after its last original compiled-out stable observation showed 31 bulk-transfer events; both original compiled-out observations are excluded. Clean retained runs have no operator events. Deck reran its full rewrite sweep, then completed parent/disabled/disabled/parent controls. Windows reran only two OFF A/A attempts before a **free-space preflight rejection** (available 1,061,818,368 bytes, required 1,073,741,824). No guest was launched for that failure. The HTTP runner exposes no supported storage cleanup action; the gate was not lowered and no SSH bypass used. [All setup failures](setup-failures.json), immutable plans, inventory pages, preflight report and every excluded result remain archived.

Remaining: free Windows storage and complete clean rewrite/parent controls; qualified cache conditions and representative game/OpenGL/graphics/resource regression checks; under-5% observer-cost evidence; actual post-clear-benefit attribution; physical remapping/spanning-page/full-flush/storage-reuse/reset/debug/concurrency safety before a retention candidate. The whole-cache clear remains unconditional.

The original evidence publication `a8dd80617e` changed documentation only and matched tested product source `0bd18bf2`. The PR subsequently received a probe-only wrapper-inlining change; its separate [followup packet](../research245-inline-probe-20260930/REPORT.md) identifies the newer source and binaries. This historical packet does not claim a product-tree match to later PR heads. [Audit](audit.py) verifies every published file, all16 host summaries/paired bootstrap intervals, source equivalence, raw guest checksums/medians, exact input/executable/mode pins, A/A arithmetic, comparison arithmetic and intervention-free retained runs. [Complete packet hashes](SHA256SUMS).
## Historical context

Prior [lighter-probe evidence](../research245-light-probe-20260930/REPORT.md) measured only hook/clear collector cost. Its [48 guest attempts](../research245-light-probe-guests-20260930/REPORT.md) all passed correctness but were cache-unqualified and measured older source `3a3d3c39`; Windows all mode showed −22.22% stable-code Improvement. They are preserved and superseded by the current observer matrix, not presented as current-head proof. [Original PGR2 checks](../research245-probe-20260930/REPORT.md) and [previous-main suite failures](../research245-cpu-fixtures-20260930/REPORT.md) remain historical; framebuffer mismatch causes are unclassified and reference hashes were not replaced.

Evidence stays in the owning xemu PR. No runner/tool evidence commit or PR is created. Draft only; never merged. Agent/model: Codex (GPT-6), with independent read-only Codex review.
