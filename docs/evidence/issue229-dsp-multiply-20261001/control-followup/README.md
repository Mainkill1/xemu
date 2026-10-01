# Issue #229: unchanged-parent controls and native pilot follow-up

## Decision: HOLD

The original native A/B table remains unchanged. The four new OpenGL A/A
attempts all execute the **same parent bytes** and pass the pinned CPU leaf
oracles, evidence and comparison gates. Their spread is larger than the
original control differences. That demonstrates meaningful variation in this
setup; it does **not** establish that the candidate is regression-free or
explain the original 5.50% worse direct-loop p95.

## A/A: identical executable, separate attempts

Campaign `issue229-deck-cpu-opengl-aa-v1`, revision
`06080f5ede8f4499b038eca2e8ff13111d72c4940bb986b28fe199fbe7839141`, reuses the
original frozen OpenGL procedure, work settings and revision-1 oracles.
All four executables have SHA-256
`4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`, parent
`ee5ce48b48784f999af374c1452003f8b2b1230f`. The planner labels B1/B2 are merely
positions: **neither contains candidate code**. Ten guest-work samples per leaf
per attempt are nested observations, not ten independent launches. Units below
are milliseconds; p95 is the attempt's nearest-rank p95 of ten samples.

| Position (all parent) | SSE mean | SSE p95 | Direct-loop mean | Direct-loop p95 | Correctness |
|---|---:|---:|---:|---:|---|
| A1 | 1179.973 | 1197.807 | 19.700 | 20.403 | Both leaves pass |
| B1 | 1207.893 | 1276.916 | 21.531 | 22.604 | Both leaves pass |
| B2 | 1177.913 | 1193.910 | 19.780 | 21.168 | Both leaves pass |
| A2 | 1179.623 | 1194.216 | 19.429 | 19.733 | Both leaves pass |

Power/frequency and background host activity were not fixed. The original
candidate means and direct-loop p95 fall inside these unchanged-parent ranges;
that is a descriptive observation, not an equivalence test or causal result.
Keep these controls separate from the fixed [original 16-attempt A/B
snapshot](../xiso-controls/README.md). The server's subsequently accumulated
cohort would contain unequal counts if recomputed; it must not replace that
table. Comparing identical A/B hashes would also manufacture a meaningless
zero-difference result.

[Raw timing records](layout/aa-records.json), [all run IDs and canonical
outcomes](aa-campaign/attempts.json), and the [complete archive](opengl-aa-artifacts.tar.gz)
are retained. Every one of its 639 members was checked against the
[path/length/SHA-256 inventory](opengl-aa-inventory.json) after compression.

## Native binary layout: limited static evidence

Both exact matching release/debug pairs identify Clang 21.1.8 as recorded in
the original build audit. All seven selected functions move by −96 bytes.
`.text` starts 96 bytes earlier and shrinks by 18,224 bytes; `.eh_frame`
shrinks by 96 bytes. `.rodata` start and size are unchanged.

| Function | Parent/candidate bytes | Resolved instruction sequences equal? |
|---|---:|---|
| `cpu_exec` | 668 / 668 | Yes, 153 instructions |
| `cpu_tb_exec` | 567 / 567 | Yes, 149 instructions |
| `helper_addss`, `helper_divss`, `helper_mulss` | 70 / 70 each | Yes, 26 each |
| `dsp_c_run` | 90 / 90 | Yes, 27 instructions |
| `dsp56k_execute_instruction` | 2077 / 2077 | Unresolved: 14 target/symbol differences |

The comparison keeps mnemonics, registers and constants, resolves direct/RIP
targets against matching debug symbols, validates x86-64 PLT slots against
imports, and identifies printable NUL-terminated strings by content hash.
Unknown addresses remain literal. LLVM module suffixes and unresolved GOT
targets in the dispatcher are retained rather than erased to force a match.
This is **not whole-binary equivalence**. Raw disassembly labels sometimes use
the nearest exported symbol; actual function boundaries come from the pinned
debug symbol table. [Addresses/build IDs](layout/layout-summary.json),
[all comparison results](layout/cpu-assembly-comparison.json), raw assembly,
normalized sequences and compressed symbol tables permit inspection.

Different instruction alignment could affect performance, but this study
does not establish that it caused the observed control slowdowns. No padding
or production layout workaround is added.

## Parent-only stationary PGR2 pilot v2: ineligible

Frozen revision
`086a5425654e44af35e542ae2a3878dbd09a132ea88c59b9c6e9ae0b81bf6e94`, full C
DSP, VP workers 0, Vulkan/vsync off, 128 MiB. It uses the same parent binary and
libraries, private guest state, fullscreen 4:3 image region, 60-second scene
warmup and 180-second observation. The image thresholds remain 5% / RGB 96;
the frame minimum remains 160. Both image gates pass, and captures show GO at
the start and opponents farther along the road at the end. This establishes
visible scene progression, not PCM correctness or fully warm shaders.

| Check | Result |
|---|---|
| Host CPU in declared segment | 197.902 core-percent mean, 363 samples |
| Positive frame intervals in final 180 s | 135; required 160, fails |
| Flip summary records in entire run | 64; mistakenly required 256, fails |
| Execution | `plan_failed`: QMP quit exceeded its existing 10 s deadline; exit 137 |
| Mesa private disk writes | Qualified, 833 files / 3,864,265 bytes; target stopped, no issues/waiver |
| Canonical comparison | Ineligible; no candidate or speedup claim |

`flipTailSamples` counts roughly five-second **aggregate records**, not
individual frames. The v2 value 256 was an investigator error requiring more
records than this run could produce. The canonical `plan_completion` check
also says passed although the final quit failed; the overall execution remains
failed and is authoritative. Both facts are preserved, without editing results.

[Summary](stationary-v2/summary.json), [assessment](stationary-v2/assessment.json),
[performance](stationary-v2/performance.json),
[start](stationary-v2/screenshots/recording-start.png)/[end](stationary-v2/screenshots/recording-end.png)
captures and the [complete 868-file archive](stationary-v2/artifacts.tar.gz)
remain accessible, with a verified [inventory](stationary-v2/inventory.json).
The frozen definition, settings and rejected prelaunch request are also kept.
The HTTP 400 request created no job; its corrected submission is the sole run.

## Follow-up and reuse

The new v3 frozen procedure uses a 300-second observation, 40 aggregate flip
records and 540 CPU samples. It retains 160 positive frame intervals and the
same image thresholds. The 600-second total-job bound covers the longer
procedure; QMP's 10-second timeout is unchanged. V2 assessments are not
retroactively repaired. A parent-only pilot must qualify before an A/B campaign.

V3 parent pilot revision `8f0c913a98217a306760fd5a87ca1104e40784cf913d5beeb0013bdaedb72d12`
now completes with exit 0 and passes all canonical correctness/evidence/
comparison gates. The start still shows GO; at the end the opponents have
progressed farther away and their minimap markers have advanced. This remains
one parent-only, cold-launch procedure with scene warmup, not proof that every
shader is warm or a fixed-work game comparison.

| V3 parent-only diagnostic | Value |
|---|---:|
| Host CPU, declared 300 s segment | 201.203 core-percent mean, 603 samples |
| Positive frame intervals, final 300 s | 236; minimum 160 passes |
| Aggregate cadence tail | 179 flips / 227.405 s = 0.787 fps |
| Frame-control interval mean / p95 / p99 | 1272.731 / 1781.167 / 1890.014 ms |
| Mesa private disk writes | 833 files / 3,864,265 bytes; no issues or waiver |

These are host `QEMU_CLOCK_REALTIME` timestamps at guest flip-control MMIO,
not guest virtual time or necessarily rendered frames. CPU uses the named
segment; frame/cadence sources use their declared final tails. Their windows
are different and must not be combined into a per-frame CPU estimate. Power,
OS page cache and driver RAM remain uncontrolled. PCM is not captured.

[Canonical outcome](stationary-v3/assessment.json), [numeric report](stationary-v3/performance.json),
[summary/identity](stationary-v3/summary.json), [frozen manifest](stationary-v3/manifest.json),
[start](stationary-v3/screenshots/recording-start.png)/[end](stationary-v3/screenshots/recording-end.png)
captures and the [complete archive](stationary-v3/artifacts.tar.gz) preserve all
865 eligible files; every member was verified against the [inventory](stationary-v3/inventory.json).
V3 establishes a usable collection procedure; it supplies no candidate result
or speedup percentage. Balanced native C/JIT comparisons remain required.

All normal execution and collection use the maintained runner HTTP clients.
Runner `848dca74e1ff79f9fc886769a785c23a0945a87e`, instance
`63e6c849eb694b3a9d11848efe26d08a`, Steam Deck/SteamOS. Native throughput,
matched warm-scene C/JIT comparisons, ordinary/reduced C settings, PCM parity
and audio underruns remain unresolved. The retained scripts reproduce the
workspace-specific inspection, freezing and evidence packaging. No evidence
is stored in either tool repository; PR #278 remains draft.

## Native C comparison: prepared before launch

[The frozen eight-attempt plan](native-balanced-v3/plan.json) declares **ABBA
then BAAB**, four independent launches per build. It uses the exact v3
revision, same 40 non-executable files and parent runtime libraries. The
[prelaunch audit](native-balanced-v3/audit.json) checks every materialized
server draft: only executable identity and parent/candidate label differ in
the fixed contract. All eight drafts were prepared before the first launch.
The passing procedure pilot is excluded from these eight comparison attempts.

A1 has been submitted; the other seven remain drafts. This is ongoing work,
with **no candidate result yet**. The runner owns execution and cleanup.
Because benchmark policy blocks job mutations during execution, submit each
next frozen ID only after the preceding run is archived and the target has
stopped; preserve its failed outcome if it fails. Do not create replacements,
filter failures, promote a baseline, or reinterpret the completed pilot as a
campaign A1. Normal commands use the maintained HTTP client:

```sh
python scripts/runner_api.py --url http://10.0.0.123:9368 submit-draft FROZEN_JOB_ID
python scripts/runner_tests.py --url http://10.0.0.123:9368 wait FROZEN_JOB_ID --job --updates
```

This native-title sequence is separate from the existing server-owned XISO
campaign planner. The preparation recipe creates drafts without starting;
the [initial submission receipt](native-balanced-v3/abba-1-submitted.json)
records the explicit first launch. Declared cache state remains cold private
launch plus scene warmup; fully warm shaders, fixed power and PCM are not
qualified. Passing coarse gates will not resolve those limits.
