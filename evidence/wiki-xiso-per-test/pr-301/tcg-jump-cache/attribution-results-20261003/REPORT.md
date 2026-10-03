# Compiler and recovery attribution on Steam Deck

Recommendation: HOLD. The full victim-cache candidate has not isolated a useful stable-code recovery gain. Inline-only improves the stable fixture and slows rewriting; both directions agree within that campaign. Recovery versus bypass is flat/inconclusive on stable code and favors recovery on rewrite, whose identical-binary control varies materially. This warrants a deliberately collision-heavy guest workload and a noncollision control before accepting cache complexity. No readiness, universal game gain or issue closure is supported.

## All comparisons

| Comparison | Test | A time | B time | Time saved A-B | Improvement % | ABBA / BAAB | Correctness |
|---|---|---:|---:|---:|---:|---:|---|
| aa | code_stable | 2.530279 s | 2.512142 s | +0.018137 s | +0.72% | +1.11% / -0.04% | 8/8 pass |
| aa | code_rewrite | 3.059612 s | 3.120064 s | -0.060453 s | -1.98% | -4.19% / +2.26% | 8/8 pass |
| inline | code_stable | 2.513865 s | 2.451782 s | +0.062083 s | +2.47% | +2.64% / +2.84% | 8/8 pass |
| inline | code_rewrite | 2.984716 s | 3.067226 s | -0.082510 s | -2.76% | -3.02% / -2.03% | 8/8 pass |
| recovery | code_stable | 2.445696 s | 2.444206 s | +0.001490 s | +0.06% | +4.81% / -0.93% | 8/8 pass |
| recovery | code_rewrite | 3.246162 s | 3.167890 s | +0.078272 s | +2.41% | +2.37% / +1.68% | 8/8 pass |

A/A uses identical parent binaries. Inline uses A=parent76c23c7d and B=inline-onlye4c8dfa6. Recovery uses A=bypassae939df5 and B=fullcandidate5679cce0. Positive Improvement %=100*(A-B)/A; negative means B is slower. Median of four attempt-level mean guest work times; ten fixed iterations per attempt. Stable performs50million operations; rewrite performs1million code-changing operations. Physical schedule A1 B1 B2 A2 B3 A3 A4 B4. These are guest work times, not FPS, host CPU seconds or suite wall time. There are four observations per label, not40 independent repetitions.

All24 attempts pass original correctness, evidence and private cold Mesa qualification without expected-result replacement, uncontrolled-cache waiver, purge or workload rerun. All7,442 artifacts are collected with zero exclusions (2,480 A/A +2,480 inline +2,482 recovery). Only the two pinned CPU leaves are covered; broader suite qualification remains unverified.

## Interpretation

Inline-only changes only the tb_lookup forced-inlining attribute, with no victim helpers or metadata. All four stable inline samples are below all four parent samples. It saves0.062083seconds per50M operations but costs0.082510seconds per1M rewriting operations. The rewrite direction is not favorable, and control variability makes its precise magnitude uncertain. The original full branch's +3.23% stable result must not be described as cache recovery alone: this simpler control produces much of that favorable direction.

Recovery versus bypass retains identical compiled dispatch caller sections/text relocation entries and retained epoch/fill/invalidation instructions. Final cpu_exec_loop, cpu_exec_step_atomic, epoch/fill/lookup and invalidator entry addresses also match. The lookup body is172bytes in the fullcandidate and3bytes in bypass; other linked layout effects remain possible. The bypass eliminates search/promotion work while retaining history/fill/invalidation overhead, so it is not a production baseline or zero-cost hook control.

Stable recovery saves only0.001490seconds (+0.06% aggregate), with +4.81% ABBA versus-0.93% BAAB. Bypass A2=2.6701571seconds versus its other runs2.436–2.456; that outlier is retained and helps explain the favorable ABBA statistic. Rewrite recovery favors the candidate by0.078272seconds (+2.41%), positive in both orders, but this compares two history-enabled builds, not the production parent. Same-binary rewrite A/A itself ranges-4.19% ABBA to+2.26% BAAB. Do not subtract independent campaign percentages or claim this supplies a net parent speedup.

Fresh updated-runner A/A is +0.72% stable with order disagreement and-1.98% rewrite with order disagreement. The preceding55a-runner A/A and its stable2.829-second outlier remain preserved in the previous recovery-controls packet; these are different campaigns, not replacement samples.

## Identity and runner maintenance

Deck10.0.0.123, Vulkan/RADV,128MiB, GCC14/O2 matched source builds, i386-softmmu, noLTO. ISO74a10c40..., catalog34b8ee7f..., suiteb45ca3dd..., sourcec02a1a44, unchanged references/configuration; full identities are in the frozen plans and package manifests. Explicit ConfigurationSource=suite; warmups0, multiplier1, per_iteration. Candidate is uninstrumented, with no observer clocks/counters. Source controls are retained on pushed branches.

Updated runner0e05af4 includes upstream1611918 (#99 completed screenshot after provider timeout). All49 RunnerChecks pass. The Linux releaseSHA5513e894... and all59payload files were verified on Deck; LAN HTTP reports the exact revision. All1,136 backed-up metadata files, configuration and preceding A/A plan/report are unchanged after upgrade. Original login credentials in the active goal resolved the prior authentication blocker; no credential is stored in the evidence. SSH was used only for operator deployment/metadata preservation; every guest launch, selection, wait and collection used maintained LAN HTTP. The first guessed /plan endpoint404 and initial build identity suffix problem remain disclosed; neither launched nor replaced a workload. The maintained plan route is ?view=plan.

## Remaining decision gates

Retain a separate source draft PR adding deliberately colliding guest PCs plus equal-work noncollision/capacity controls and independently computed execution checksums. Qualify it before adding its ISO/catalog to comparisons; no selfapproval of framebuffer references. If it establishes useful recovery, continue real VM remap/second-page, reset/load/SMC/storage recycling/exhaustion and inside-helper concurrency tests, then broader XISO and reached retail/cross-renderer/resource checks. PGR2 measurement requires at least300seconds in a validated reached scene. Inline-only is a useful simpler direction but its rewrite tradeoff also prevents readiness. Full unit rebuild remains failed in unchanged resampler math declarations; full suite success is not claimed.

## Evidence

EVIDENCE.zip retains every public attempt outcome, measurement, raw guest result, launch/input manifest, full JSON/Markdown/CSV report, cache qualification, frozen definition and maintained sequence/collection/verification tool. INDEX.json inventories all files by local SHA256/size; private runtime-state/cache bytes and redundant diagnostics.zip remain manifest-only. No measured sample or failed qualification is omitted. These records belong to owning xemu draft301, not evidence commits in the runner or test-suite repository.
