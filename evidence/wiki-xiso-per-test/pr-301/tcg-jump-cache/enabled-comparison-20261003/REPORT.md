# Actual enabled eight-entry candidate: matched Deck comparison

Decision: HOLD. The branch shows a promising stable-code improvement in both orders. Rewrite does not establish a gain and remains inconclusive. This is an end-to-end candidate result, not proof that victim recovery alone caused the change: the enabled build also forces tb_lookup inlining after GCC otherwise outlined it. Compiler/code-shape ablation and a deliberate-collision guest workload remain necessary to understand the useful mechanism.

## Results

| Test | Backend | Parent | Enabled candidate | Time saved | Improvement % | ABBA | BAAB | Correctness |
|---|---|---|---|---|---|---|---|---|
| code_stable | Vulkan | 2.530310 s | 2.448594 s | +0.081715 s | +3.23% | +2.88% | +4.38% | 8/8 pass |
| code_rewrite | Vulkan | 3.109760 s | 3.135581 s | -0.025821 s | -0.83% | +0.77% | -2.56% | 8/8 pass |

Statistic: median of four attempt mean guest useful-work times; ten fixed-work iterations per attempt. Physical order A1 B1 B2 A2 B3 A3 A4 B4. `code_stable` performs 50 million operations; `code_rewrite` performs one million code-changing operations. The source at c02a1a44 fixes these budgets and checksums. Positive Improvement % = 100*(parent-candidate)/parent; time saved = parent-candidate. These are guest work times with unchanged emulator clocks, not display FPS, host CPU-seconds or total suite duration.

All four stable-code candidate samples (2.421–2.476 s) are below all four parent samples (2.506–2.597 s). Both orders favor the candidate. This supports another bounded experiment, not universal retail improvement or readiness. Rewrite switches from +0.77% ABBA to -2.56% BAAB; its aggregate -0.83% is within the observed control variation and remains inconclusive. The runner report's overall verdict is inconclusive because one metric disagrees by order; every attempt is individually comparison-eligible.

## Controls and identities

Fresh identical-binary A/A: stable apparent +0.38% (+0.54% ABBA, +0.27% BAAB); rewrite apparent -4.35% (-4.30% ABBA, -1.76% BAAB). Default-off versus parent: stable -0.33%; rewrite +0.72% aggregate with +3.25% ABBA and -1.87% BAAB. These independent controls are variation checks, not corrections to subtract and not a universal noise floor. All control and candidate samples, including unfavorable ones, are retained in the linked packets.

Parent source: 76c23c7d444a6f12c9778bb2c35fab513f6c8056; executable SHA 5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b.
Candidate product: 5679cce099eb777977e761245de47e77d05d0c05; enabled SHA 51cd99ef27b7212ba75029c489b31b36b44f732302cee27276c2c62756357c24.
Both GCC14/O2 i386-softmmu, matching dependencies and settings; new option is enabled only for the candidate. The enabled branch has no observer counters/clocks. Additional allocation is 160 bytes per CPU; primary offset and 4,096-entry layout remain unchanged.

Steam Deck 10.0.0.123, Vulkan/RADV, 128 MiB; runner 55a360daf82a699ecd12d69d8484f32bf1cf3752; original suite b45ca3dd..., ISO 74a10c40..., catalog 34b8ee7f..., references and configuration. Exact hashes and launch identities are in enabled-plan.json, PACKAGE manifests and each input/launch manifest. ConfigurationSource=suite; warmups 0, multiplier 1, per_iteration. No SSH launch, manual intervention, reference replacement or benchmark rerun.

All eight runs complete with exit code zero, correct pinned CPU outcomes, complete evidence and verified cold private Mesa namespaces with observed writes and no waiver/issues. Collection includes 2,483 artifacts with zero exclusions. Three runs contain one additional private cache file; all public artifact sets match. No critical/assert/fatal/error line was found in the retained stderr scans. Suite-wide qualification remains unverified and only the two CPU leaves were selected. Across A/A, OFF and ON, all 24 XISO attempts are correct and cache-qualified; the separate initial component evidence-threshold failure remains preserved.

## What this implements and does not prove

The candidate consults eight owner-private FIFO entries after a primary miss; validates virtual PC/CS/flags/exact cflags, promotes a match, and retains a displaced valid identity only across a stable generation. Real full/page/targeted clears bump generation and active invalidator count, preserving original primary writes. Postpublication validation rejects overlapping fills. Existing exclusive/serial TB storage lifetime remains.

Thirteen actual-helper cases pass locally and on Deck; four deliberate safety mutations are rejected. Source review found no concrete defect, but real VM remapping/second-page remapping, reset/load/SMC/recycling/storage exhaustion, inside-helper invalidation interleavings and other hosts remain unqualified. Full local unit rebuild fails on the unchanged resampler missing math.h; this is not a passing full suite. OpenGL and representative retail/game frame/resource comparisons are not yet measured. PGR2 qualification requires a reached, validated workload for at least 300 seconds.

## Next bounded work

1. Separate compiler/code-shape effects from recovery with source-bound controls; prefer the simpler useful change if the cache does not add benefit.
2. Add a deliberate-collision guest benchmark with independent correctness and a noncollision control. Retain its source in a separate draft XISO-suite PR, then qualify it before benchmarking.
3. If useful gains persist, complete mapping/lifecycle, broader XISO, cross-renderer and reached retail coverage. Keep owning xemu draft #301 HOLD until the relevant gates pass; no merge.

## Evidence

EVIDENCE.zip contains every public candidate measurement, full per-test reports, raw guest results, frozen plans, input/launch manifests, metrics, cache reports and reusable workflow scripts. INDEX.json inventories all collected artifacts by local SHA-256 and size; private state/driver-cache bytes and redundant diagnostic ZIPs are manifest-only. No measured sample or failure is dropped. Other packets in this owning PR preserve the local build failures, first report-index race, both native helper attempts and the collector's initial mistaken rejection of an inconclusive verdict.
