# Issue #229: retained C DSP multiply

## Decision: HOLD

The independently implemented native multiply is faster in both compilers and both balanced orders on this authoring host. Actual synthetic C interpreter instruction execution improves by 19.85–25.08% with GCC and 6.66–9.72% with Clang. These are fixed-work arithmetic/instruction results, not game FPS, audio-thread savings, Steam Deck results or default JIT gains. All 20 platform/project CI jobs pass; PCM/native C-backend qualification and unaffected JIT controls remain required.

## Baseline, candidate and scope

- **A / parent:** accepted fork main `ee5ce48b48784f999af374c1452003f8b2b1230f`.
- **B / candidate code:** `1e6cebe0cb6452f875329710fa19795f8644ad2d`.
- Product change: only `dsp_mul56()` in `hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c`. Four partial multiplies/carries and multiword shift/subtraction become signed 24-bit decoding, one native 64-bit multiply, optional negation and unsigned fractional scaling/splitting. Existing 8/24/24 results, caller flags, accumulation, rounding and dispatch are retained.
- Default `use_dsp_jit=true` and all JIT code remain unchanged. The retained C path is reached when JIT is disabled; no native title profile has established its share of host time.
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

## Native/XISO coverage still missing

No audio PCM comparison, native C-versus-C title benchmark, JIT-versus-JIT control, Deck or Windows run is included. Do not infer a normal-game gain from the synthetic table. Required qualification retains audio settings and compares matched backends/settings/builds; instrumented profiling is separate from throughput runs.

The maintained shader XISO categories have no direct DSP/audio workload. A focused bare-metal-capable DSP XBE covering signed multiply/MAC/rounding/flags and PCM output is suggested; it is not implemented on this branch. Existing CPU/XISO leaves are unaffected whole-emulator controls, not proof of DSP correctness. Read-only inspection of the registered catalog identifies `cpu_floating_point.sse_scalar` (revision 1) and `cpu_translation_blocks.direct_loop` (revision 1) as planned unaffected controls. The [catalog response](deck-cpu-control-catalog.json) and [inventory identity](deck-control-catalog-identity.json) retain all seven CPU leaves without truncation. This older suite is unverified inventory, not a qualified #229 campaign; a compatible parent/candidate campaign and references still need to be frozen before execution.

| Required measurement | Backend | Parent | Candidate | Difference | Improvement | Correctness |
|---|---|---|---|---|---|---|
| `cpu_floating_point.sse_scalar`, `cpu_translation_blocks.direct_loop` | OpenGL | Not yet measured | Not yet measured | N/A | N/A | Not run |
| `cpu_floating_point.sse_scalar`, `cpu_translation_blocks.direct_loop` | Vulkan | Not yet measured | Not yet measured | N/A | N/A | Not run |
| Native DSP-using title, C backend and PCM parity | OpenGL/Vulkan separately | Not yet measured | Not yet measured | N/A | N/A | Not run |
| Same title, unaffected JIT control | OpenGL/Vulkan separately | Not yet measured | Not yet measured | N/A | N/A | Not run |

The Deck's existing Mesa cache qualification blocker is independent of these CPU-only tests. Runner draft [#80](https://github.com/Mainkill1/Xemu-Test-Runner/pull/80) addresses that tool behavior; no evidence is stored in the runner or test-tool repositories. Native qualification remains HOLD until trustworthy measurements are available. PR #187 remains deferred for later review.

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
