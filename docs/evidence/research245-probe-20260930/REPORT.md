# Issue 245 / PR 272: jump-cache attribution

**Status: Investigating — diagnostic collector only; no production Improvement demonstrated.**

## Baseline and candidate comparison

| Role | Identity / meaning | Result |
| --- | --- | --- |
| Fixed cycle baseline | `bd1fecb93353272dda2a810991e28945de35b665` (retained cycle baseline; not tested in this stage) | No matched measurements; Improvement **not measured** |
| Previous main | `2d289cb349bca95eae81b6a54b0f8d965365ff82` | Existing product behavior; [baseline selection](../../performance/baseline-selection.json) distinguishes it from the fixed cycle baseline |
| Diagnostic candidate | `06168a2f455b832bc6eb7936ebe725ec530b1b00`, draft [PR 272](https://github.com/Mainkill1/xemu/pull/272) | Counts existing clearing/lookup work; keeps unconditional invalidation |
| Behavioral optimization candidate | **None** | No lazy retention implementation or production Improvement result |

Positive Improvement would mean better useful guest progress or lower CPU/frame
intervals. Missing matched measurements stay explicitly unmeasured. Collector
ON/OFF observations measure instrumentation; they are not baseline-versus-optimized
product results. No faster xemu candidate has been established by this stage.

## Processing flow and run names

Existing main clears all 4,096 jump-cache slots after an individual PC-relative TB invalidation. This draft adds optional counters around that same unconditional clearing. No stale-entry retention or cache-size change exists.

“ON” enables the collector; “OFF” disables it in the **same diagnostic executable**. Numbers are repetitions on that host. “Menu control” selects the profile/title menu instead of the race. OFF repeat 3 is an additional Windows execution after repeat 2 retained incomplete frame analysis. Old folder names remain in the raw records.


## Decision

Retain the existing invalidation behavior. The opt-in probe in [draft product PR #272](https://github.com/Mainkill1/xemu/pull/272) establishes frequent PC-relative whole-cache clearing in PGR2 and measures the population observed before clearing. This is a diagnostic implementation with no lazy retention or speedup claim.

Roughly 52–55% of race-phase clears observed no populated slots. Mean occupancy was 169–185 of 4,096 slots (about 4%), while interval totals still contain millions of non-null observations. Low mean occupancy alone neither proves that preservation is useless nor identifies which subsequent global lookups were caused by clearing. The probe itself measurably disturbs Deck cadence. Keep its timings separate from production comparisons.

The additional menu capture is **rejected as a low-invalidation control**: it remained near 950 individual invalidations/s. Morrowind, a CPU-heavy self-modifying workload and a genuinely low-invalidation control remain pending. This is partial first-gate evidence for [issue #245](https://github.com/Mainkill1/xemu/issues/245).

## Exact inputs and procedure

- Product source: `06168a2f455b832bc6eb7936ebe725ec530b1b00`; tree `67c893a6de2adadc92557aad7b9e86da4dc14926`.
- Parent/current main: `2d289cb349bca95eae81b6a54b0f8d965365ff82`.
- Linux executable SHA-256: `9e2dbe1b9ff3298de99f57c9c00accab7cd25dad6362e7d92d7d4d63aed0bd53`.
- Windows executable SHA-256: `0d70a1f4403a064636ed717b9a8abc4a03f04f4610addf883626f5b1a04e069d`.
- Visible version: `0.8.136-0-g06168a2f455b`; retained build identities include exact version headers, package hashes and configure arguments.
- Linux GCC 14.2.0; Windows static MXE cross build with Clang 21.1.8. The earlier current-main CI executable uses a different build/toolchain; its timing is not an observer baseline for these binaries.
- Runner/client source: Deck/client `6089e8b841bce379015500853c0455551d7fd2cf`; Windows `079e3d483523b3e53a11983efd40e2c530747b5f`.

Normal tests used the maintained runner HTTP clients from the build host. ON and OFF on each host use the **same executable bytes**, private guest state and saved procedure; only the collector environment and experiment labels differ. HMP `info jit` snapshots bracket a named 30-second scene. Snapshot intervals use diagnostic invocation start times, approximately 31–33 seconds including screenshot/query overhead. Actual counter reads occur inside those queries.

Deck ordering was ON1, OFF1, OFF2, ON2, MENU1. Windows was ON1, OFF1, OFF2, OFF3, ON2; OFF3 is an intentional additional run after OFF2's frame analyzer failure. Each execution has a distinct request identity. Assets were retained/catalogued rather than transferred during measurements. Definitions and revision receipts are retained alongside the results; no global cache policy was relaxed.

All twenty start/end screenshots were inspected. The nine race captures show 000 MPH; Windows start captures include the introductory camera/countdown, while ends show the stationary rear camera. Deck race captures show the rear camera at both boundaries. MENU1 remains on the animated profile/title menu at both boundaries. Windows and Deck race targets/seeds differ, so these are independent host observations. Screenshot visibility checks alone are not scene or emulation correctness proof.

## Direct attribution

Values are differences of independently atomic HMP rows. Mean occupancy counts pointer observations, not unique discarded TBs. Global-hit percentage is the share of completed dispatch lookups that missed the jump cache and found an existing TB in the global table. It does not establish post-flush causality.

| Capture | PC-relative whole-cache clears | Approximate rate | Mean observed slots | Empty clears | Global-hit share | Sampled clear elapsed mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows collector ON repeat 1 | 38,210 | 1169.58/s | 168.79 (4.12%) | 55.02% | 5.35% | 2.052 µs (1,258 samples) |
| Windows collector ON repeat 2 | 37,625 | 1160.17/s | 170.86 (4.17%) | 55.47% | 5.82% | 2.527 µs (1,243 samples) |
| Deck collector ON repeat 1 | 38,792 | 1241.88/s | 185.22 (4.52%) | 51.55% | 12.62% | 5.888 µs (1,275 samples) |
| Deck collector ON repeat 2 | 34,058 | 1090.39/s | 180.77 (4.41%) | 51.90% | 12.59% | 5.909 µs (1,118 samples) |
| Deck collector ON menu control | 29,614 | 952.47/s | 107.22 (2.62%) | 54.05% | 12.57% | 5.820 µs (995 samples) |

| Capture | Jump-cache hit calls | Global-hit calls | Global-miss calls | New publication arrivals | Invalid-TB reuse arrivals |
| --- | ---: | ---: | ---: | ---: | ---: |
| Windows collector ON repeat 1 | 351,587,470 | 19,857,582 | 58,477 | 11,017 | 38,189 |
| Windows collector ON repeat 2 | 319,364,472 | 19,739,549 | 57,724 | 10,907 | 37,642 |
| Deck collector ON repeat 1 | 116,176,971 | 16,781,558 | 50,739 | 5,336 | 38,787 |
| Deck collector ON repeat 2 | 99,538,807 | 14,343,279 | 45,160 | 5,196 | 34,048 |
| Deck collector ON menu control | 60,692,033 | 8,732,465 | 36,111 | 1,184 | 29,606 |

PC-relative clear deltas equal the existing individual invalidation deltas in every ON capture. Full **TB** flush count remains zero throughout all ten intervals. Whole-cache `other_flush` deltas are 3 and 4 in the Windows ON captures and zero in the Deck ON/menu captures. Page-selective clears are uncounted. No non-PC-relative targeted removals were observed in the ON captures.

The completed lookup totals match started totals across these intervals, and each clear histogram sums to its calls with exactly 4,096 traversed slots per clear. This observed agreement does not turn the independently read fields into a coherent snapshot. Cumulative maxima and their before/after values are retained in `summary.json`; subtracting them would not yield an interval maximum.

New publication arrivals include possible duplicate translations later discarded and temporary translations. Invalid-TB reuse is **not retranslation**. Neither count attributes later translation work to a particular clear.

Flush samples include the collector's additional occupancy reads and timer overhead. Sampled lookup costs also include timer overhead; total instrumentation overhead includes additional atomic accounting. The sampled means are not uninstrumented path costs. The original current-main five-second profile and its ~3.15% weighted self-cycle lookup sample are preserved in the [first-stage report](../research245-20260930/REPORT.md); this probe is not a replacement production profile.

## Runner performance checks and unfavorable outcomes

These metrics are copied from the retained runner `performance.json`, without recomputing benchmark metrics from CSV. CPU is process core-percent. Guest cadence is distinct from rendered FPS. OFF leaves the collector disabled inside the diagnostic build; a normal build excludes its code entirely.

| Capture | Mean CPU | Guest cadence | Guest frame p99 (ms) | Actual maximum (ms) | Required evidence |
| --- | ---: | ---: | ---: | ---: | --- |
| Windows collector ON repeat 1 | 406.11% | 30.00/s | 34.568 | 34.808 | complete |
| Windows collector OFF repeat 1 | 400.08% | 30.01/s | 34.439 | 34.862 | complete |
| Windows collector OFF repeat 2 | 397.13% | 30.00/s | unavailable | unavailable | incomplete |
| Windows collector OFF repeat 3 | 390.54% | 30.00/s | 34.038 | 34.731 | complete |
| Windows collector ON repeat 2 | 398.65% | 30.00/s | 34.305 | 34.674 | complete |
| Deck collector ON repeat 1 | 316.70% | 19.89/s | 74.671 | 95.485 | complete |
| Deck collector OFF repeat 1 | 317.35% | 21.57/s | 69.018 | 90.467 | complete |
| Deck collector OFF repeat 2 | 317.67% | 21.66/s | 69.495 | 83.379 | complete |
| Deck collector ON repeat 2 | 290.31% | 17.45/s | 102.975 | 132.979 | complete |
| Deck collector ON menu control | 203.89% | 59.84/s | 23.286 | 39.199 | complete |

All ten executions completed, exited zero and passed their declared correctness checks. Nine have complete required evidence. Windows collector OFF repeat 2 remains **incomplete**: the runner reports `frames: Analysis source changed while reading.`, retains monitoring/flip analysis, and provides no frame-tail result. OFF3 completed with full required evidence. The failed analysis, original request and raw sources are retained; OFF3 does not erase it.

Deck OFF cadence repeats closely at 21.57–21.66/s; ON captures are lower at 17.45–19.89/s and vary more. Windows stays near 30/s, with CPU ranges overlapping across ON/OFF. This demonstrates a material observer effect/variation, not a calibrated universal overhead percentage. Host clocks/power were not locked, repeat count is small, and Windows analysis collector duty is substantial. An enabled diagnostic is unsuitable for declaring an optimization win.

Deck results are comparison-ineligible because driver-cache namespace control remains unverified. The older Windows runner labels complete results eligible, but it does not supply the current cache isolation qualification here; these diagnostic measurements are **not accepted production comparisons**. No current-main versus fixed-baseline optimization result is claimed.

## Implementation verification

- Full Linux probe build, Windows probe cross build and full Linux default-OFF build linked successfully. Modified sources introduced no compiler warning lines; retained logs also show unrelated warnings in unchanged code/dependencies.
- Four production-collector unit tests pass: unconditional atomic clearing/preserved PCs, disabled behavior, accounting/sample distribution/formatting, and concurrent writers. ASan/UBSan also pass 4/4.
- The retained pre-implementation negative-control log fails on a cache pointer left uncleared. The initial full link failed because Meson omitted the collector source; corrected source-set wiring passes both final links. These are setup/development failures, not successful test runs.
- Default-OFF config has no probe define, and `nm` finds no collector symbols. Instrumented package bytes were preserved before reconfiguring that local build.
- New files use repository clang-format settings; diff checks pass. Independent review found no blocking implementation defect and its interpretation limits were included in the code-linked usage documentation.
- Existing QHT/QTree unit controls passed separately. All 20 jobs in [CI run 36714772023](https://github.com/Mainkill1/xemu/actions/runs/36714772023) pass at the exact product commit, including Linux, Windows, macOS and the unit job. CI uses the default-OFF probe option; the local collector tests above explicitly use the enabled option.

Run `python3 audit.py` from this directory to verify the ten retained outcomes, executable/source identities, available runner analysis source hashes, and counter/histogram invariants; it regenerates `summary.json`. `SHA256SUMS` pins public artifact bytes. Executables, guest disks, firmware, game media and driver-cache binary contents are excluded.

## Remaining gates

Before a behavioral experiment: obtain the missing workloads/control; attribute lookup/retranslation effects more precisely; establish complete stale-entry validity, mapping/spanning-page, storage reclamation, reset/load, debug/cflags and concurrency checks. Then measure a separately built, uninstrumented candidate against previous main and the fixed cycle baseline, with A/A noise and the requested balanced schedules. No cache-shape or unrelated CPU/APU optimization is bundled here.

Keep product #272 draft; the former tool-repository evidence PR #48 is superseded by this product-repository report. No merge, readiness transition or issue closure is requested.

## Run identities

| Capture | Runner run ID |
| --- | --- |
| Windows collector ON repeat 1 | `20260930-122442020-f2eb56d465cf4b2193e3566eece73794` |
| Windows collector OFF repeat 1 | `20260930-122930272-368bce91dbb145fc8049c76b64a95faf` |
| Windows collector OFF repeat 2 | `20260930-123420420-3974eb7340674e7da218e99b692e123d` |
| Windows collector OFF repeat 3 | `20260930-123840968-81128be50a0449399db97fde7355ad40` |
| Windows collector ON repeat 2 | `20260930-124154616-e291d4f37756451cbc88609366f5e570` |
| Deck collector ON repeat 1 | `20260930-122434866-01143cf45bd9440cb93d9f2faaaff2b8` |
| Deck collector OFF repeat 1 | `20260930-122922062-e2fad00bf0304954ab600e37d0607523` |
| Deck collector OFF repeat 2 | `20260930-123411941-b798f05dfd0d4d61ae38225385552a87` |
| Deck collector ON repeat 2 | `20260930-123832542-5f55c9d73e344cc9a935120cd9593358` |
| Deck collector ON menu control | `20260930-124316611-f40b32d2974e4b0083713b069ad6bdf8` |

Agent/model: Codex (GPT-6), with read-only code/evidence review by a Codex subagent using the inherited model.

## Evidence location

Raw records now live in **Mainkill1/xemu**, with the code in PR 272. `ORIGINAL-REPORT.md` and `SOURCE-SHA256SUMS` retain the original source snapshot. Run `verify_migration.py` to check original/current bytes. All failed analysis and comparison exclusions remain.
