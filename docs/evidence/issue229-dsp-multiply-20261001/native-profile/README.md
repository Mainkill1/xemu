# Native C DSP profile and runner recovery

**Decision: HOLD.** This diagnostic confirms that PGR2 reaches the retained C DSP interpreter. It does not measure a parent/candidate game improvement. Existing synthetic instruction gains remain valid for their stated workloads; the native game, PCM and XISO qualification requirements remain open.

## What the profile establishes

One 30-second `cycles:Pu` capture sampled the accepted-main parent on Steam Deck, PGR2, Vulkan, full DSP enabled, DSP JIT disabled, cold private Mesa disk cache. The input procedure reached the countdown/early race; it did not qualify a stable racing window. Parent source is `ee5ce48b48784f999af374c1452003f8b2b1230f`, executable SHA-256 `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`. No candidate profile was recorded.

| Self-cycle grouping | Weighted share of process samples | Samples | Interpretation |
|---|---:|---:|---|
| xemu executable | 66.26% | 4,118 | All sampled executable functions |
| libsamplerate library | 24.11% | 1,496 | Resampling is another observed cost, outside this patch |
| Named C/DSP functions | 38.50% | 2,105 | Confirms substantial C/DSP execution in this forced-C diagnostic |
| Multiply-family instruction functions | 3.31% | 180 | Whole MPY/MAC instruction functions, including work beyond multiplication |

Shares use each record's cycle **period**, not raw sample counts or sums of rounded display percentages. Total: 6,812 samples, zero reported losses, period 151,296,884,201. Groups overlap: executable includes the two named function groups. The C/DSP group is the explicit `^(dsp_|dsp56k_|emu_)` symbol-name filter within xemu; it includes shared DMA functions and omits generic memory helpers. The multiplier group is `^emu_(mac|mpy)` within xemu. Neither is an exhaustive causal accounting of interpreter work.

Examples: `dsp56k_execute_instruction` 11.60%, `emu_update_rn` 4.44%, `dsp_c_run` 4.29%, and `emu_mac_p_y0_x0_a` 1.70%. The changed `dsp_mul56` helper is inlined; this capture does not isolate its cost. Applying the synthetic speedup to the entire 38.50% group would overstate the affected work. These shares are neither a predicted FPS gain nor a hard upper bound.

## Exact capture and symbolization

Frozen procedure: `issue229-deck-pgr2-full-dsp-c-profile-v1`, revision `386ff280da43992f12cbd51ca5d0ba864e600f84aa19bc925ee995bf3e7d84ee`. The recipe requests 30,000 ms at 99 Hz with `dwarf,4096` call stacks. Runner revision at capture: `0ba533ed89c8bb3cf530c4bfff8dbb190b3f08be`, instance `ab50c5cb96884152998523afb762d683`.

`perf record` succeeded (28.374 MB reported). The runner's subsequent default call-graph report exceeded its unchanged 60-second deadline. The [complete diagnostic ZIP](parent-profile-diagnostics.zip) preserves the raw recording and original result; SHA-256 `c21e39b030a416be94bb1554bc2392cd957b68d88fe91164106510ef759ea453`. All 32 bundle-manifest entries were independently checked for length and SHA-256.

Offline `perf 6.12.101` recovered a self-only report with call-graph expansion disabled. Correct symbolization required the exact parent executable **and** its runtime libraries at the recorded mmap paths inside the private symfs, alongside the matching debug file. Build ID: `7cc47cc0deb81956e198d71935d1a198116cdb02`; debug-file SHA-256 `614ec7555af52d876e51de31263126b7fe700aac87c9592c218a959a24fffb0b`; GNU debuglink CRC-32 `0xe57e9ead`. The debug package and executable identities match. No global debug cache was changed.

The recorded executable path relative to symfs is:

```text
home/deck/xemu-research/workspace/Queue/Testing/
  agent-issue229-deck-c-parent-profile-20261001/xemu
```

Place the parent `lib/` bundle beside it and the matching debug file under `usr/lib/debug/.build-id/7c/c47cc0deb81956e198d71935d1a198116cdb02.debug`. [Command arguments](command.json) preserve the exact invocation. [All self-report rows](mapped.tsv), [parsed rows](rows.json), and [weighted summary](summary.json) preserve the calculations. [summarize-perf.py](summarize-perf.py) regenerates the rows and grouping summary, rejects truncated period coverage, and refuses to overwrite an output directory:

```sh
python3 summarize-perf.py mapped.tsv --out /absolute/new-summary-directory
```

The initial debug-only symfs produced incorrect symbols because it lacked the matching mapped ELF/library files. [Its compact record](superseded-wrong-symbol-map.json) is retained as **superseded and invalid**. None of its percentages supports a conclusion here. Separately, attempts to obtain both CI jobs' compiler logs returned empty files, so compiler/options matching for native A/B packages is still unqualified.

## Preserve both failed attempts

| Attempt / run ID | Result | Interpretation |
|---|---|---|
| Original `20261001-162746650-9a3fecbf40d5444aa610e51450f3972d` | `cleanup_failed`; runner stopped | Completed diagnostic timeout was incorrectly classified as a live component; raw profile survived |
| Automatic retry `20261001-163656280-a3546f341dc74aaab9b0a8b56ae49005` | `unresponsive`; operator intervention | Recovery automatically retried the same queued job; supported HTTP quit stopped it; image/evidence checks remain failed |

Both ran job `issue229-deck-c-parent-profile-20261001`. The operator restart initially assumed a final result would prevent a retry. Runner recovery explicitly excludes `cleanup_failed` from durable completion and retries it under the existing policy. This created the second attempt; it was not an independently requested or balanced run. No queue or attempt journal was edited. Recovery rewrote the original on-host result as an interruption receipt; the **original complete failure result remains intact in the ZIP and `original-attempt/`**. The retry's authoritative result/assessment and operator activity are retained in `automatic-retry/`. The original exit 137 reflects runner termination of the target, not evidence of an emulator crash.

`operator-recovery/` contains inspection, service restart, idle-state check and binary upgrade receipts. Previous binaries, configuration, assets and results were preserved. Configuration SHA-256 remains `8d0265ba728e1f01100a9c579ef9c888abdb5053a12b7277ba7f3eba34b0bd60`. SSH was confined to this separate operator workflow; test staging, submission, retrieval and canaries used maintained HTTP clients.

## Runner fix and native check

Draft [runner PR #82](https://github.com/Mainkill1/Xemu-Test-Runner/pull/82), stacked on [Mesa PR #80](https://github.com/Mainkill1/Xemu-Test-Runner/pull/80), changes one exception filter: a timeout counts as a stuck component only if the task is still incomplete. Completed diagnostic failures retain `plan_failed` and allow the queue to continue. Live components still block reuse. Tool deadlines, recovery policy and comparison gates remain unchanged.

| Check | Result |
|---|---|
| Retained integration regression before fix | 39 passed; new test fails with runner IOException |
| After fix and review | 40 passed, zero failed; verifies timeout detail and subsequent queued completion |
| Project/platform CI | All 21 checks pass at `848dca74e1ff79f9fc886769a785c23a0945a87e`, including Windows/Linux runner checks |
| Deck diagnostic timeout canary | Expected `plan_failed`, actual `plan_failed`, diagnostic `TimeoutException` after 15,000 ms |
| Immediately following native quit canary | Expected `completed`, actual `completed`, exit 0, same runner instance |
| Final runner state | Idle, no current job/process, queue pending/testing both zero; 2 finished, 1 expected failure |

These are **runner reliability checks**, not game correctness or performance qualification. Both canonical canary assessments have `Correctness=notEvaluated` and are comparison-ineligible; the timeout's canonical execution/evidence failure is intentionally retained. Neither canary weakens or retroactively passes the failed game pilots.

Deck deployment: runner `0.2.0+848dca74e1ff79f9fc886769a785c23a0945a87e`, instance `63e6c849eb694b3a9d11848efe26d08a`, executable SHA-256 `e15168d2d317a44f31a3ca33de69f3531521581215d24ae1f6c37422eb8341a5`. The Windows operator recovery still deployed `0ba533ed`; no Windows native #229 result is claimed.

Canary runs:

- Timeout: `20261001-170325961-be9789a08bcc4f5aa3dc3c365a7ebc17`.
- Follow-up: `20261001-170354959-c6d973283a7c48f6a633a3fd9383ed7f`.

`runner-native-canary/` holds both results, assessments, pinned inputs and same-instance/idle receipts. `frozen-procedures/` retains each immutable definition and revision. The preparation scripts retain the executed, workspace-specific HTTP recipes; they are not portable runner replacements. This package is stored with the owning xemu change; the runner repository contains its actual code/test fix only.

## Clock correction and remaining work

The earlier pilot report incorrectly called the flip timestamp a separate guest clock. `nv2a_profile_increment()` uses `qemu_clock_get_us(QEMU_CLOCK_REALTIME)`: these are **host-clock timestamps of guest flip-control accesses**. The pilot's last-five flip window still does not establish alignment with the selected host CPU segment, actual presented frames, or a fixed-work workload. The earlier pilot explanation is corrected; no timing values or eligibility decisions are changed.

Next required evidence: a frozen and verified stable native scene, controlled warm-cache state and matching builds, balanced ordinary/reduced C comparisons with JIT controls, PCM parity, and per-leaf XISO controls. No FPS gain is claimed. The 24.11% resampler share is a possible separate research direction, not a change included in this PR.
