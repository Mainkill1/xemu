# Issue #229: retained C DSP multiply

## Decision: HOLD

The independently implemented native multiply is faster in both compilers and both balanced orders on this authoring host. Actual synthetic C interpreter instruction execution improves by 19.85–25.08% with GCC and 6.66–9.72% with Clang. These are fixed-work arithmetic/instruction results, not game FPS, audio-thread savings, Steam Deck results or default JIT gains. All 20 platform/project CI jobs pass. Three native Deck pilots are now retained, all ineligible under their declared frame contract; PCM/native C-backend qualification and unaffected JIT controls remain required.

## Baseline, candidate and scope

- **A / parent:** accepted fork main `ee5ce48b48784f999af374c1452003f8b2b1230f`.
- **B / candidate code:** `1e6cebe0cb6452f875329710fa19795f8644ad2d`.
- Product change: only `dsp_mul56()` in `hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c`. Four partial multiplies/carries and multiword shift/subtraction become signed 24-bit decoding, one native 64-bit multiply, optional negation and unsigned fractional scaling/splitting. Existing 8/24/24 results, caller flags, accumulation, rounding and dispatch are retained.
- Default `use_dsp_jit=true` and all JIT code remain unchanged. The retained C path is reached when JIT is disabled; the parent-only native profile below confirms reachability but does not isolate multiplier cost or establish an A/B game gain.
- Interpreter inputs are byte-identical between the parent and fixed `baseline/cycle-01-start` (`9f618d6d8c4c446ef023955f3d4de22f661f61a4`): see [source-equivalence manifest](cycle-baseline-source-check.json). This is module equivalence, not cumulative full-emulator qualification. The fixed baseline was not moved.
- [Identity manifest](IDENTITY.json) binds exact source, test and executable hashes. Later evidence-only commits retain the measured product and tests unchanged.

## Read the measurements

`helper` = private production multiplier with natural compiler inlining. `helper-call` = the same multiplier through a volatile function pointer, forcing identical call boundaries for both revisions. Both consume 4,096 deterministic pre-generated random operand/sign triples. Natural helper results include the compiler deciding to inline the smaller candidate; they do not isolate arithmetic alone.

`mac` = actual interpreter fetch/dispatch of repeated MAC Y0×X0→A. `mixed` = all 128 parallel MPY/MPYR/MAC/MACR opcode variants in a repeating 4,096-word program. These two lanes use fixed synthetic source registers and include real instruction flags, rounding, accumulation, fetch, dispatch and PC handling. They are representative synthetic instruction loops, not recorded audio microcode or titles.

All lanes consume result checksums; instruction lanes consume all accumulator words and status. Warmup is 65,536 operations outside the timing interval; its checksum also must match. Empty memory barriers and no-inline batch functions keep work between timing boundaries. Optimized assembly retains dynamic arithmetic and calls; see the linked `.asm` files in this directory. GCC helper bodies shrink from 310 to 109 bytes, with both matched-call loops 205 bytes. No runtime dispatch abstraction or test hook is added to production.

**A/A** runs the unchanged parent four times. **ABBA** means parent, candidate, candidate, parent; **BAAB** reverses that order. Eight rounds of each order yield 16 parent and 16 candidate attempts per order, or 32 of each per lane, plus the four parent controls. Primary campaigns contain 272 attempts per compiler, 544 total; all completed successfully and timed/warmup checksums match. Nothing is dropped. Each helper attempt performs 50 million operations; each instruction attempt performs 10 million.

Times are host monotonic nanoseconds per operation, calculated from whole-batch elapsed microseconds. Positive improvement is less time: `100 × (parent − candidate) / parent`; the difference column is parent minus candidate. Medians are per variant within each order; percentages are ratios of those medians, not FPS gains.

| Compiler | Workload | Order | Parent ns/op | Candidate ns/op | Saved ns/op | Improvement |
|---|---|---|---:|---:|---:|---:|
| GCC | `helper` | ABBA | 5.772 | 1.146 | +4.626 | +80.15% |
| GCC | `helper` | BAAB | 5.772 | 1.146 | +4.626 | +80.14% |
| GCC | `helper-call` | ABBA | 5.790 | 1.792 | +3.998 | +69.05% |
| GCC | `helper-call` | BAAB | 5.786 | 1.791 | +3.996 | +69.05% |
| GCC | `mac` | ABBA | 11.733 | 9.395 | +2.338 | +19.93% |
| GCC | `mac` | BAAB | 11.724 | 9.396 | +2.328 | +19.85% |
| GCC | `mixed` | ABBA | 21.563 | 16.169 | +5.395 | +25.02% |
| GCC | `mixed` | BAAB | 21.595 | 16.180 | +5.415 | +25.08% |
| CLANG | `helper` | ABBA | 2.851 | 1.150 | +1.700 | +59.65% |
| CLANG | `helper` | BAAB | 2.853 | 1.149 | +1.704 | +59.74% |
| CLANG | `helper-call` | ABBA | 3.265 | 1.559 | +1.706 | +52.26% |
| CLANG | `helper-call` | BAAB | 3.262 | 1.558 | +1.704 | +52.24% |
| CLANG | `mac` | ABBA | 7.855 | 7.111 | +0.744 | +9.47% |
| CLANG | `mac` | BAAB | 7.860 | 7.096 | +0.764 | +9.72% |
| CLANG | `mixed` | ABBA | 10.321 | 9.633 | +0.687 | +6.66% |
| CLANG | `mixed` | BAAB | 10.311 | 9.620 | +0.691 | +6.70% |

### Variation and limitations

Both binaries use matching release-style options: `-O2`, LTO, `-march=x86-64-v3`, `-fwrapv`, no strict aliasing, zero automatic initialization and used-register clearing. GCC is 14.2.0; Clang is 19.1.7. This is Debian 13 on AMD Ryzen AI MAX+ 395, pinned to logical CPU 0. Frequency boost, power policy and other shared-host load were not controlled; these observations are not a portable latency guarantee. [Environment](environment.json) and compile argument JSONs preserve those choices.

The four A/A controls are a small noise check, not a confidence interval. GCC parent A/A ranges: helper 5.820–5.918, matched-call 5.802–5.815, MAC 11.717–11.761, mixed 21.466–21.768 ns/op. Clang: helper 2.886–2.939, matched-call 3.258–3.261, MAC 7.865–8.129, mixed 10.257–10.358 ns/op. Improvements remain present in both balanced orders. Different compilers and code layout produce materially different instruction results, so the table includes both rather than selecting the largest result.

`perf stat` hardware counters are unavailable (`perf_event_paranoid=3`, permission denied); [failed PMU attempt](perf-probe.log) is retained. There is no instruction/cycle/branch-counter claim.

## Correctness and build checks

- Actual unchanged parent passes the boundary/random helper oracle and real instruction replay before the product edit.
- Final matching GCC/Clang parent and candidate binaries each pass all four retained GLib tests; final candidate UBSan also passes without diagnostics. Compile logs are empty, with `-Wall -Werror` for standalone checks.
- Corpus: 242 boundary/sign combinations, 2,000,000 deterministic random/sign comparisons, 22 operand-high-bit checks, plus 46,464 real instruction executions across all 128 opcode variants and three scaling modes. Cross-register initialization covers fewer independent pairs for some opcode variants; helper arithmetic still receives the full Cartesian boundary/random corpus. This is not full DSP instruction conformance.
- Complete register/status/PC/cycle/idle digest matches parent: `7591ff4ee4a7f31b8971da505208bda80e84b492c451fb27164c860f39e07772`.
- Deliberate negative controls remove fractional scaling or swap words in separate parent source copies. Both fail the boundary assertion (exit −6), proving the oracle detects those defects; [scaling failure](missing-fractional-shift.log), [word-order failure](swapped-result-words.log). They do not alter the committed product.
- Standalone compilation reuses generated headers and `libqemuutil.a` from support source `b7adeca8281b1c30983df58f8a845b7fb8355677` and compiles the exact parent/candidate DSP translation unit. It is **not a fresh full product build**. Project Meson registration and full platform builds are now independently verified by [CI run 36881642099](https://github.com/Mainkill1/xemu/actions/runs/36881642099): 20/20 jobs pass, covering Linux/Windows/macOS x86-64 and ARM64 debug/release builds and packaging. The project unit suite reports 137 passed, 16 skipped and 0 failed; the new DSP test passes 4 subtests in 0.28 seconds. The separate Xvfb/Mesa depth draw test passes 17 subtests. [CI summary](ci-summary.json) and [unit/skip record](ci-unit-summary.txt) retain exact job links and all skipped test names. CI packages merge commit `b9359dbf55c72bd7e8569f677dc4aafd771592d7`, whose tree is verified identical to branch head `778338b8ba38484ff1afecf6e69e513e482658e9`. Subsequent evidence-only commits leave every product/test source unchanged; their documented equivalence avoids repeating platform builds for report updates. An identical-head push-triggered run was deliberately canceled to keep one complete PR-triggered build matrix.
- Read-only review found no blocking arithmetic defect; two benchmark checks were corrected: matched warmup checksums and rejection of below-resolution timing attempts.

## Native pilots and remaining qualification

Three short PGR2 Vulkan pilots ran on the Deck with full DSP enabled: C parent, C candidate and JIT parent. **All are NOT COMPARABLE** because each failed the 160-frame/25-second evidence gate; the C pilots also failed whole-image brightness checks. Screenshots show countdown/startup rather than a verified steady racing window. The [native pilot report](native-pilot/README.md), [summary](native-pilot/SUMMARY.json), frozen definitions and raw artifacts preserve every attempt. No game improvement percentage is claimed. This procedure must be corrected under a new frozen revision before a native ABBA/BAAB campaign.

No PCM parity, qualified native C-versus-C game comparison or balanced JIT game control exists yet. Required qualification includes ordinary and reduced-work DSP settings, controlled warm-cache state and matching builds; instrumented profiling remains separate from throughput runs. Windows preparation initially failed for lack of disk space; its pilot was never executed. Operator recovery preserves original assets/evidence and moves runtime/catalog storage to D without lowering free-space gates.

The maintained shader XISO categories have no direct DSP/audio workload. A focused bare-metal-capable DSP XBE covering signed multiply/MAC/rounding/flags and PCM output is suggested; it is not implemented on this branch. Existing CPU/XISO leaves are unaffected whole-emulator controls, not proof of DSP correctness. The two existing revision-1 CPU leaves `cpu_floating_point.sse_scalar` and `cpu_translation_blocks.direct_loop` now have a [balanced native control campaign](xiso-controls/README.md): 16/16 attempts pass correctness/evidence and are eligible, with 32 exact leaf-oracle checks. OpenGL and Vulkan use separate frozen definitions, shared parent runtime libraries and unchanged historical oracles. These are JIT-enabled, unrelated controls, not C DSP or whole-ISO qualification. Mean timings are mixed and OpenGL direct-loop p95 is 5.50% worse; the cause remains unresolved. Initial inventory queries remain retained.

| Required measurement | Backend | Parent | Candidate | Difference | Improvement | Correctness |
|---|---|---|---|---|---|---|
| `cpu_floating_point.sse_scalar` | Vulkan | 1175.756 ms | 1187.375 ms | -11.619 ms | -0.99% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Vulkan | 19.870 ms | 19.726 ms | +0.144 ms | +0.73% | 8/8 leaf checks pass |
| `cpu_floating_point.sse_scalar` | Opengl | 1178.236 ms | 1196.858 ms | -18.622 ms | -1.58% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Opengl | 19.891 ms | 20.091 ms | -0.200 ms | -1.00% | 8/8 leaf checks pass |
| PGR2 full-DSP C pilot | Vulkan | Frame/image gates failed | Frame/image gates failed | N/A | NOT COMPARABLE | Both ineligible |
| Native title, qualified C backend and PCM parity | OpenGL/Vulkan separately | Not yet qualified | Not yet qualified | N/A | N/A | PCM not captured |
| PGR2 full-DSP JIT pilot | Vulkan | Frame gate failed; image checks passed | Not yet run | N/A | NOT COMPARABLE | Parent ineligible |
| Balanced unaffected JIT controls | OpenGL/Vulkan separately | Not yet qualified | Not yet qualified | N/A | N/A | Incomplete |

Runner draft [#80](https://github.com/Mainkill1/Xemu-Test-Runner/pull/80) is deployed on the Deck and all three native pilots pass the schema-2 Mesa storage gate without a waiver. This resolves the disk-namespace qualification blocker for these fresh runs, not their failed game evidence contracts. Complete storage ledgers stay in this xemu evidence package. Native performance remains HOLD; PR #187 remains deferred.

## Native CPU controls: passing correctness, mixed timings

A=parent and B=candidate; one ABBA and one BAAB block per renderer yield four independent attempts per build/backend. The table above uses the runner's median of per-attempt leaf means, guest-reported fixed-work milliseconds, with 10 nested samples per attempt and warmups3/multiplier4. This is not host CPU time or FPS. All 16 attempts pass schema-2 private Mesa storage qualification without a waiver, and all fixed inputs match within each backend. The stored suite remains unverified as a whole.

[All 48 server comparison rows](xiso-controls/final-comparison.csv), raw guest results/receipts, per-attempt timing records, frozen procedures and verified archives are retained in [xiso-controls/](xiso-controls/README.md). Controls worsen by 0.99–1.58% in three mean-time rows, improve 0.73% in one, and OpenGL direct-loop p95 worsens 5.50% (+1.153 ms). Power/frequency were not fixed; the causes remain unclassified. No regression-free or native game gain claim follows from these passes. Controlled repeat/build-layout checks remain needed.

Matching debug packages identify Ubuntu Clang 21.1.8 for both native DSP translation units, and eight prescribed build/workflow files match. Exact native compiler-command logs remain absent. Earlier standalone Clang 19.1 measurements retain their original identity.

## Control follow-up and revised native pilot

Four additional OpenGL A/A attempts all use the exact unchanged parent executable and pass both CPU leaf oracles, evidence and comparison gates. SSE attempt means span **1177.913–1207.893 ms**, direct-loop means **19.429–21.531 ms**, and direct-loop p95 **19.733–22.604 ms**. Original candidate points fall inside these parent ranges; this demonstrates variation, not candidate equivalence or a causal explanation. The original 16-run A/B snapshot stays fixed and is not recomputed with these additional parent samples.

The pinned native binary study finds matching resolved instruction sequences in `cpu_exec`, `cpu_tb_exec`, three SSE helpers and `dsp_c_run`; all seven selected functions move by −96 bytes. The dispatcher still has 14 unresolved target/symbol differences. Instruction alignment is a possible performance factor, not a proven cause; there is no whole-binary equivalence claim or layout workaround.

The longer, fullscreen parent-only PGR2 v2 pilot passes image gates and visibly progresses from GO to opponents farther down the road. It remains **ineligible**: 135 positive intervals in the final 180 seconds fall short of the unchanged 160 minimum, and the erroneous `flipTailSamples=256` asks for aggregate five-second records rather than individual frames. It also fails its existing 10-second QMP quit deadline and exits 137. The canonical `plan_completion` subcheck says passed, but the overall execution failure is authoritative. Mesa qualification passes without a waiver. All failed artifacts and definitions remain unchanged.

The corrected v3 parent-only pilot now **completes with exit 0 and passes all canonical gates**: 603 CPU samples, 236 positive frame intervals in the final 300 seconds (minimum 160), and 179 flips across the final 40 aggregate records / 227.405 seconds. Its diagnostic cadence is 0.787 fps and frame-control interval p95 1781.167 ms. Those are host-clock flip-control observations, not necessarily rendered frames. Private Mesa writes remain qualified without a waiver. Both captures show progression, but fully warm shaders, fixed power and PCM parity remain unqualified. A single passing parent establishes the procedure, not a candidate gain.

[Complete A/A table, static analysis, pilot outcomes, reproducible recipes and verified archives](control-followup/README.md) retain this follow-up in the owning xemu repository. Native game gains, PCM parity, power controls and the original unaffected-control slowdowns remain unresolved; #278 stays draft/HOLD.

## Native profiling and runner reliability

The [recovered Deck parent profile](native-profile/README.md) confirms C-backend reachability in PGR2 with full DSP/JIT disabled. Weighted self-cycle shares: named C/DSP functions 38.50%, multiply-family instruction functions 3.31%, libsamplerate 24.11%. Groups overlap; multiply functions include instruction work beyond the inlined helper. These are one diagnostic window, not an A/B comparison, FPS prediction or hard upper bound. No candidate profile exists.

The raw capture succeeded, but reporting timed out and exposed a runner exception-classification bug. Both the original `cleanup_failed` attempt and the automatically retried, operator-stopped attempt remain retained and ineligible. Draft [runner #82](https://github.com/Mainkill1/Xemu-Test-Runner/pull/82) fixes the completed-task timeout classification and keeps the live-task gate. Its retained integration check fails before the fix and passes afterward; all 21 CI checks pass. Deck native canaries independently show the expected diagnostic `plan_failed`, immediately followed by `completed` on the same runner instance. These check runner reliability; canonical game correctness remains unevaluated. The Deck now runs `848dca74e1ff79f9fc886769a785c23a0945a87e`; Windows remains on `0ba533ed`.

The prior pilot clock explanation is corrected: flip-control timestamps use host `QEMU_CLOCK_REALTIME`, not guest virtual time. Their last-five window still does not establish a matched steady-scene measurement. No values or assessment gates were changed.

## Reuse and raw evidence

Retained tests live in `tests/unit/test-xbox-mcpx-dsp-mul.c` and its Meson registration. The opt-in balanced driver is `tests/unit/benchmark-xbox-mcpx-dsp-mul.py`. It records executable SHA-256, affinity, all stdout/stderr and failures before rejecting bad attempts; output directories cannot overwrite a campaign.

After building matched test executables (same test source, compiler/options/support; parent includes its exact `dsp_cpu.c` via `XEMU_DSP_CPU_SOURCE`), run:

```sh
python3 tests/unit/benchmark-xbox-mcpx-dsp-mul.py   --parent /absolute/parent-test-binary   --candidate /absolute/candidate-test-binary   --cpu 0 --out /absolute/new-campaign-directory
```

- Primary [GCC raw attempts](gcc-balanced-v2/attempts.jsonl), [summary](gcc-balanced-v2/summary.json), [manifest](gcc-balanced-v2/manifest.json).
- Primary [Clang raw attempts](clang-balanced-v2/attempts.jsonl), [summary](clang-balanced-v2/summary.json), [manifest](clang-balanced-v2/manifest.json).
- Superseded preliminary three-lane campaigns: [GCC](gcc-balanced/attempts.jsonl), [Clang](clang-balanced/attempts.jsonl). Their 408 attempts are preserved but do not certify the final four-lane benchmark binaries. No attempt failed; the primary matrix supersedes them after review added the matched-call lane. Total retained attempts: 952.
- Compile argument JSONs, unit logs, negative-control logs and optimized assembly reside beside this report. Large local binaries remain in the workspace artifact directory; identities permit correlation without committing executables.

## Attribution and related work

Addresses [fork issue #229](https://github.com/Mainkill1/xemu/issues/229). The optimization idea is credited to Will Bonnett / Synkronicity's [Symphony commit](https://github.com/Synkronicity/Xemu-Symphony/commit/6927121a40ef9df78eeff79ea031f0771e62c516); only commit metadata was consulted, not its implementation diff. The code is independently written from the target's arithmetic contract. Target GPL/copyright headers remain. The pre-implementation design is [recorded separately](../../performance/issue229-dsp-multiply.md).

Upstream [#3047](https://github.com/xemu-project/xemu/pull/3047) proposes retiring the C interpreter and was still open at this investigation. That limits the long-term value of this backend-specific improvement; no JIT or AGU optimization is bundled.
