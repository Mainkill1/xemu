# #167 Linux context: retained diagnostics, variation remains unclassified

## Decision

**HOLD for xemu PR #301.** No emulator policy or runtime code changed. Four same-executable diagnostic attempts reproduce materially different ten-target timings while recording host context. This identifies uncontrolled inputs and preserves a useful tool; it does not prove a performance improvement or the cause of variation. The circular FIFO remains rejected.

## Quick look

| Diagnostic | 10 targets | 2 targets | 8 targets | Noncolliding control | Samples | Collector median / max | Observed vCPU core changes |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1.254020 s | 0.557992 s | 0.596593 s | 0.366709 s | 64 | 13.656 / 22.970 ms | 35 |
| 2 | 1.045881 s | 0.556642 s | 0.600460 s | 0.375789 s | 64 | 13.974 / 22.540 ms | 27 |
| 3 | 1.271111 s | 0.547962 s | 0.611377 s | 0.370297 s | 64 | 14.010 / 19.656 ms | 41 |
| 4 | 1.259271 s | 0.551309 s | 0.610113 s | 0.364758 s | 64 | 13.500 / 20.896 ms | 29 |

Each row is one new **instrumented diagnostic** attempt, same original FIFO executable. Guest work means use all ten samples of 8,000,000 calls per leaf. These are seconds of fixed guest work, not FPS. No baseline/candidate ratio is computed: none of these attempts is clean comparison evidence. Collector values are its own wall time per snapshot, not emulator overhead percentages or CPU time.

All four attempts execute successfully and pass the four reviewed arithmetic CPU contracts. All evidence is complete: **1,260/1,260 artifacts collected**, no exclusions or reruns. Private cold Mesa namespaces qualify on all four; no cache waiver. The canonical comparison outcome is **ineligible**, correctly because each attempt contains a declared diagnostic. Each records exactly one diagnostic and zero pauses/manual inputs. Rendering and the other 159 leaf contracts remain unqualified.

## What was built and why

Retained [runner source draft #105](https://github.com/Mainkill1/Xemu-Test-Runner/pull/105), head `ef8c76369bee0821dc839d83da753d7c813d2a24`, contains `tools/linux_performance_context.py`, eleven regression/CLI checks and the documented external recipe. It reads procfs/sysfs and emits JSON Lines. It introduces no C# service/API, implicit sampler, host policy write or emulator instrumentation. It is packaged and hashed with the test application and uses the existing external diagnostic adapter. Normal test control stays LAN HTTP; no SSH was used for these diagnostic attempts.

Context fields include per-thread start identity, user/system ticks, last executed CPU, allowed CPUs and context switches; CPU driver/governor/limits/reported readings; GPU DPM/hwmon readings; first process mappings and ASLR policy. PID and TID start identities are checked around snapshots; reuse discards mixed records. Missing/denied values are explicit rather than zero. All three diagnostic pause/resume flags are false. Source review initially caught default pause behavior and a thread identity race; both were corrected before native use.

## What the context establishes

- CPU policies report `amd-pstate-epp` with `powersave`; vCPU affinity remains `0-7`. The observed last CPU changes 27–41 times between 64 snapshots. This is an observed-change count, not the total number of migrations.
- Reported policy frequencies across cores/readings range about 1.550–3.504 GHz. `cpuinfo_cur_freq` is absent for all eight policies. Reported `scaling_cur_freq` is not guaranteed to be measured hardware frequency; these values do not establish throttling or stable delivered clocks.
- ASLR policy is `2` and all four executable text mappings have different addresses. The same executable bytes do not imply an identical host layout. This does not prove layout caused the fast ten-target occurrence.
- Sampler median collection cost is 13.5–14.0 ms, worst 22.970 ms. It has a material observer cost and the sampler may disturb host/device scheduling. Never treat its work timings as a replacement for clean measurements.
- Earlier archived clean A/A slow attempts did not show swap increases or collector overruns. Their whole-process CPU/GPU averages were similar; they cannot allocate host effects to an individual leaf.

There are no per-leaf host timestamp boundaries in the current guest result contract. Context spans boot/setup and workload. No exact leaf/frequency alignment, causal estimate or confidence bound is claimed. Different policy readings, CPU placement, ASLR layout and shared CPU/GPU budget remain hypotheses requiring controlled ablation. No power, affinity, ASLR, driver, cache or emulated-time policy was changed.

## Immutable identities

- xemu product: original FIFO `5679cce099eb777977e761245de47e77d05d0c05`, executable SHA-256 `51cd99ef27b7212ba75029c489b31b36b44f732302cee27276c2c62756357c24`.
- Runner deployed source: `e17919c849b840d43171a615b288f559fbec9e4c` on Deck `10.0.0.123`, Vulkan/RADV, 128 MiB.
- Collector source: `ef8c76369bee0821dc839d83da753d7c813d2a24`; exact script SHA in `sequence-contract.json`/package identity.
- Application: `i167-5679-linux-context-ef8-v1`; upload alone did not start it.
- Saved template: `i167-linux-context-template-v1` @ `e51d395108a7e4ffe8fd78f44dda77301d3e962cadadba265142a59d6de73369`.
- Suite: `i167-linux-context-v1-suite` @ `4bad4376896ebf986ad63df038e90fba8fce637961c38c382bc846886dfa7b9d`.
- Same fixture source `19f252d79aed0c3614ec43fab1d468f09ff6296e`, ISO `87296b1b35e9f3ade67661ed53d42e9d11ff786bf9ecb07182149c9924c7767f`, reviewed arithmetic reference `db086d21eb0e600f30246c1a8bc129290dcce0f16f4308ead9765f35cd49876b`. Zero warmups, multiplier one, per_iteration, explicit suite configuration.
- Predeclared campaign IDs: `i167-linux-context-20261003-001` through `-004`, all frozen before the first start. Each contains one monolithic attempt.
- Run IDs, all 160 raw guest samples, host samples, command evidence, plans/results and canonical ineligibility reasons are in the linked ZIP and summary.

## Source checks and preserved failures

Baseline main Python suite: 89 pass. New missing-feature checks fail before implementation. Final targeted checks: 11 pass; full Python suite: 100 pass. A negative control accepting process reuse fails the expected test before restoration. The thread reuse race also reproduces RED before its fix. Read-only review reports no remaining blocker. Local HTTP fixture execution alone clears inherited SSH markers; production guard remains unchanged.

An inspection guessed a nonexistent `xiso` job key and failed before mutation; subsequent inspection used the actual `workload.guestHddResults` contract. No diagnostic or measurement was retried. All diagnostic attempts finish and Deck returns idle.

## Next bounded research

The large above-capacity regression remains a product issue despite uncertain small differences. Use the retained context for explicit diagnostic investigations of CPU placement/layout/policy, then require fresh clean A/A before evaluating a new admission/search-cost candidate. Preserve fit-case benefit, invalidation/storage safety and controls. Broader real VM lifecycle, OpenGL, reached-game and resource qualification remain missing. Do not declare the host variation fixed or advance PR #301 to ready.

[All public raw evidence](EVIDENCE.zip) · [All collected artifact hashes and exclusions](INDEX.json) · [Context summary](SUMMARY.json). Firmware/private state bytes and diagnostics ZIPs remain local with hashes recorded; no emulator evidence is stored in the runner source PR.
