# #190 / #275 cost diagnosis — 2026-10-05

Product branches remain #190 `8e996641` and #275 `7dcdcb0d`. Nothing in this evidence branch is intended to merge into main.

## Decision

Retain the current mapping design. The tests below demonstrate useful reader savings but do **not** establish a repair for the prior release-to-release Deck Morrowind regression. The owner accepts merging if a failure in the changed source cannot be identified; source provenance is being checked against an exact-main artifact. Do not substitute diagnostic FPS for release qualification. Reject the outlined-refill experiment; it regressed Clang's eight-worker changing-ADPCM workload.

## Same-executable reader experiment

Diagnostic-only `26e5c757` provides startup-selected generic reads, descriptor caching, and descriptor-plus-payload caching. Each mode uses the same executable/compiler layout, snapshot, saved controller inputs, and authored scene interval. Counters retain current guest-value observations and are owned by each voice worker. Outer scopes sample approximately one in 128 operations. Per-word clocks are disabled. Preparation includes 24 production VP checks and 600 state cases; Linux/Windows native artifacts were compiled by CI run 37262566472.

| Sampled callback mean, µs | Generic | Descriptor | Combined |
|---|---:|---:|---:|
| Deck, lightweight thread observation | 4.270 | 3.734 | 3.666 |
| Windows, WPR active | 2.153 | 1.945 | 2.006 |

These are one diagnostic run per cell with different evolving audio mixtures, not statistically qualified gains. The Deck combined path had about 96.0% descriptor hits and 88.8% payload hits, no descriptor mapping changes, and no physical payload fallback. Windows also used both caches; its apparent descriptor/combined callback difference cannot be assigned to the reader without equal-work normalization.

The Deck's fixed-work native fixture supplies the stronger isolated comparison: all 12 cases pass with independently verified one/eight busy workers. Combined versus descriptor VP elapsed improves 3.95%/2.58% for changing stereo ADPCM; PCM differs by less than 0.3%. These are single samples compiled with GCC14/O2, not release Clang/ThinLTO gameplay qualification.

## Profiling correction and remaining cost

Hardware perf recording materially inflated worker kernel CPU on Deck: roughly 7.6–7.9 CPU-seconds during the 20-second capture, versus 2.6–2.9 in the subsequent lightweight `/proc` capture. Perf also lost samples. Those traces cannot establish a scheduling regression. Windows WPR likewise perturbed sampled dispatch waits; its three completed ETLs have zero lost events, but capture/rundown extends beyond the requested interval and raw totals cannot be compared as scene work. An initial Windows profile failed because PowerShell blocked file execution; its completed game/counter capture is retained separately. Subsequent launches used a process-local execution-policy override.

Lightweight Deck data shows approximately 17.83 / 17.09 / 16.24 user µs per voice call, while scheduled CPU is 26.12 / 26.57 / 25.87 µs. These normalizations do not make different voice mixtures equivalent. Eight workers still dispatch around 1,498 times/s. There is no measured large kernel or runqueue penalty cancelling the reader savings. Sparse mapping/refcount samples do not establish cacheline contention.

The named guest CPU/TCG thread receives about 96% of one core and has less than 0.12 CPU-second of runnable delay over 20 seconds. The data does not show starvation by audio workers. This identifies the next boundary for diagnosis; utilization alone does not prove a frame bottleneck.

## Code-layout experiment rejected

Moving payload mapping setup/fallback to an outlined helper preserved semantics in review and passed 24 VP plus 600 state cases under both GCC14 and Clang19. It reduced the containing function by 309/263 bytes, but increased total text and introduced a call on misses. The trial also used likely branch guidance; it is a combined layout experiment, not isolation of the noinline attribute alone.

All 64 fixed-work ABBA/BAAB output/cursor checks passed. Clang eight-worker changing stereo ADPCM nevertheless regressed **3.92%** overall, adverse in both orders (**1.82% / 4.24%**). The experiment is not promoted to either product PR.

## Actual-release audit

The exact deployed main-equivalent reference has build ID `a82835e00420c35c7d319fd1f2b1d4d4314d22f1`; its source differs from main only in test files. Its matching symbols confirm Clang21.1.8, as do #190/#275. A previously inspected source-equivalent pull-build symbol file had a different build ID and is not used as this executable's identity.

Across 38,285 common text-symbol names, only `voice_resample_callback` changes size: 5,866 to 6,944 bytes from main to the combined stack. All 1,462 matched TCG/helper functions retain their sizes, but their addresses change (mostly +2,272 bytes). Sizes do not prove instruction equivalence, and address changes do not prove an alignment slowdown. Between #190 and #275, matched TCG/helper addresses and sizes are unchanged.

## Build provenance qualification

The comparison reused a clean CI artifact built at `59e5a983`; it did **not** freshly build exact main `b4d69b24`. Its three differences from main are test files, with no non-test differences. #190's CI merge tree is identical to its PR head. #275 is the two-commit sample-reader delta above #190. No unrelated unmerged runtime PRs were found, and the diagnostic branch is excluded. See `source-audit.json`. An isolated CI configuration now checks out and builds immutable exact main; its workflow branch is separate from the product checkout.

## Actual-release critical path and separate TCG opportunity

One sequential TCG-only counter capture per build shows approximately19 CPU-seconds per20 wall-seconds, with little runnable delay. Around1096 draw-flush calls/frame occur in every build; nested draw-flush elapsed rises from14.03 to15.92ms/frame from the reused reference to combined. Executed instructions/frame differ, and sequential thermal/order conditions are not equivalent. These findings do not establish a layout cause or GPU regression. See `actual-release-critical-path.txt`.

Subsequent IP-only user-cycle captures lose zero samples:3,875 reference and3,859 combined. `lookup_tb_ptr_common` plus `helper_lookup_tb_ptr_i32` account for27.60%/25.24% of sampled cycle weight. Generated guest code remains unresolved. This is a substantial independent hotspot, not evidence that the audio change caused it. Static TCG addresses/sizes are identical between190 and275. Broad FlatView/MMIO consumers are sparse; persistent mapping changes lack causal support.

[Issue310](https://github.com/Mainkill1/xemu/issues/310) tracks the regression/provenance; [issue311](https://github.com/Mainkill1/xemu/issues/311) tracks translated-block lookup attribution. The owner accepts merging if a defect in changed source cannot be established. That acceptance does not mark the performance discrepancy solved.

## Remaining

The prior release-to-release -2.57% Deck Morrowind cadence result remains unresolved. The next useful comparison must attribute the guest-CPU/rendering critical path or establish a reproducible layout effect; another broad game campaign or speculative cache rewrite would not answer that question. No benchmark inputs, baseline, resampler setting, worker-count policy, or product code were changed by this diagnosis. Both rigs were left idle.

Raw ETL/perf, game screenshots, kernel addresses, resource bytes, and process-memory evidence remain private under the Codex workspace.
