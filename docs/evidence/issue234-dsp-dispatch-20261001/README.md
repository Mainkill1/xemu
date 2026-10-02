# Issue #234: retained C DSP dispatch experiment

**HOLD / draft. Synthetic replay gains are real for these batches, but several lanes regress and native candidate qualification is pending. No game or JIT speedup is established.**

Baseline **A** is fork main `ee5ce48b48784f999af374c1452003f8b2b1230f`. Candidate **B** changes only the C interpreter's dispatch/cache representation and reset invalidation. It does not include #229's multiply patch. The C interpreter remains selectable; JIT is the default. Upstream #3047 was still open and unmerged at the start of this investigation.

## What changes

Replace the existing per-P-memory-slot opcode-entry pointer with a compatible one-argument handler pointer. On a cache miss, choose the existing normal or parallel handler; on a hit, call it. Fetches, extension reads, instruction cycles/length, PC/REP/DO/interrupt processing and tracing remain in their existing paths. A cold wrapper preserves unimplemented-opcode diagnostics. P writes still invalidate their slot; existing bootstrap/invalidation/VM-load clearing applies to the replacement table. Reset also clears derived entries. No host pointers enter saved VM state.

Measured host table size is unchanged: **8 bytes per slot, 4,096 slots, 32 KiB per C core**. No second decoded table, repetitive instruction validation, spin heuristic, arithmetic substitution, or JIT rewrite is introduced.

Study attribution: Will Bonnett / Synkronicity's [predecode work](https://github.com/Synkronicity/Xemu-Symphony/commit/e8c7c38a3e8cd6c746aa53d59a8214957a300cdd), as identified by the issue. Implementation uses xemu's own handler signatures, memory-write boundaries and existing opcode tables. No Symphony implementation expression was copied. The attribution also appears adjacent to production dispatch; existing DSP copyright/license notices remain.

## Why investigate this region

The retained #229 Steam Deck C-backend diagnostic profile uses exact parent executable SHA-256 `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`, ELF build ID `7cc47cc0deb81956e198d71935d1a198116cdb02`. Its raw profile SHA-256 is `a822f876f40c1e37cac3b7f6561031a60139b596174d920dd1167381ee158cea`. The matching debug binary, rather than the different GCC local build, was used for address attribution.

That original attempt had cleanup failure, failed image correctness and incomplete frame evidence. Its raw CPU samples are useful **only as diagnostic attribution**. They are not a passing performance comparison. Original canonical result/failures and the raw capture are retained in [#229's native-profile evidence](../issue229-dsp-multiply-20261001/native-profile/).

`perf annotate` segfaulted (exit 139); the stderr and optimized disassembly are retained. `perf script -G` supplied leaf IPs, symbol offsets and cycle periods. Summing the exported records independently reproduces **6,812 samples and 151,296,884,201 cycle period**, with 639 samples / 17,552,678,227 period in `dsp56k_execute_instruction` (**11.60%**). This is whole-function self cost, not all dispatch.

| Exact function offsets, half-open | Region | Process sampled cycles |
|---|---|---:|
| `+0x78..+0xa0` | Classification and cached normal-entry access | 2.6119% |
| `+0xa0..+0x135` | Normal decode miss | 0.0000% |
| `+0x135..+0x13e` | Normal handler-pointer indirection | 0.5749% |
| `+0x18e..+0x1a1` | Parallel handler selection | 0.1704% |
| `+0x1a1..+0x1a6` | Common argument setup and indirect call | 1.7327% |

The selection regions sum to **3.3572%**. This is a rough affected-region ceiling for this observation, not a predicted whole-game gain: cache access and a miss branch remain, the indirect call remains, and the parallel path acquires cache work. Sampling skid and this short capture limit individual-instruction interpretation. Zero miss samples does not prove zero misses. [IP histogram](parent-ip-histogram.json), [leaf records](parent-leaf-samples.txt.gz), [assembly](parent-disassembly.txt).

Fresh exact-parent C and JIT Deck profiles are prepared through maintained runner HTTP clients. They use private cold Mesa launch namespaces, identical stationary-scene navigation, 60 s scene warmup and a 90 s observation with a 30 s CPU capture. These are diagnostics, not code A/B qualification. C attempt `20261001-235059559-3e9986b3fc7b4b5e856691af1bdffbe3` completed and passed source correctness, but its frame analysis failed with `Insufficient positive frame intervals`; evidence remains incomplete/comparison ineligible. JIT attempt `20261001-235605297-a17798163c884a8ca1b4cae5b6cba65d` is still active at this checkpoint. No result is repaired or waived.

## Replay performance

[Full baseline/candidate table](replay-table.md). Positive improvement means lower candidate elapsed time: `100 × (parent − candidate) / parent`. Values are **nanoseconds per emulated instruction**, from whole-batch monotonic timing; they are not FPS, host utilization, or native-game measurements.

Each compiler's final **v2** campaign contains **576 successful attempts**: six workloads × three physical orders × eight blocks × four launches. Every batch executes 5,000,000 instructions. `AAAA` runs four parent launches; `ABBA` runs parent/candidate/candidate/parent; `BAAB` reverses that order. Every A/B row has 16 parent and 16 candidate observations. All final architectural register/memory digests match per workload. No attempts were excluded. Earlier v1 campaigns and their different binary identities are also retained; v2 follows the addition of tracing coverage and is the reported checkpoint.

| Replay | GCC improvement: ABBA / BAAB | Clang improvement: ABBA / BAAB |
|---|---:|---:|
| Normal | **−9.71% / −9.61%** | +9.94% / +9.87% |
| Parallel | +12.59% / +12.78% | +13.56% / +13.66% |
| Mixed | +7.54% / +9.86% | +30.18% / +25.63% |
| Working set | +7.84% / +3.95% | +25.21% / +21.71% |
| Cold table | +1.80% / +1.45% | **−3.05% / −3.00%** |
| Identical-word P writes | +2.55% / +2.30% | −0.71% / −0.14% |

### Workload meanings and limits

- **Normal:** 32 slots executing the production immediate control-register move into LA, without active DO state.
- **Parallel:** 32 slots executing the production no-move parallel `CLR A` handler.
- **Mixed:** alternate those two instructions across 32 slots.
- **Working set:** the same alternating stream across all 4,096 P slots.
- **Cold table:** the 4,096-slot stream, with a full table clear at each program pass. The timing includes clearing; it does **not** isolate first-ever opcode decode or a novel-opcode miss.
- **Identical-word P writes** (CLI `updates`): a production P-memory write before every dispatch, even when rewriting the same instruction. This measures write invalidation and cache repopulation, not changing-program semantics. Actual differing-opcode mutations and DSP self-writes are covered by correctness tests.

PCs are selected by the replay driver. These are reconstructed synthetic patterns using two instruction bodies, not captured title programs or autonomous guest loops. Counts and inputs are prepared outside timing except deliberate write/clear work. Tracing and diagnostic counters are disabled during performance runs. The final digest consumes register and X/Y-memory state after timing. It is not a PCM oracle.

Both compilers use O2, debug info, x86-64-v3, identical paired flags and the same support library/configuration. GCC 14.2 uses LTO; Clang 19.1.7 uses ThinLTO/LLD. These are explicit standalone settings, not a claim of matching the product's CI flags. CPU affinity is logical CPU 0. Frequency policy was unchanged and shared-host contention is uncontrolled; small differences and extrema need caution. AAAA ranges are retained in each summary. PMU counters were unavailable (`perf_event_paranoid=3`); the failed request is preserved, without modifying host policy.

[Binary identities/function sizes](binary-identities.json), [GCC final manifest/summary](gcc-balanced-v2/manifest.json), [GCC all final attempts](gcc-balanced-v2/runs.json.gz), [Clang final manifest/summary](clang-balanced-v2/manifest.json), [Clang all final attempts](clang-balanced-v2/runs.json.gz). Full summaries live alongside each manifest. [Frozen measured fixture](measured-replay-fixture-v2.c) retains the v2 replay; later source changes only add explanatory comments. [Compiler wrapper](build-replays.py) and per-binary `*-argv.json`/logs record the exact compilation commands and support build path.

## Correctness and build checks

Ten production-boundary tests pass with GCC, Clang and UBSan: warmed normal/parallel overwrites; caching at the last supported P slot; reset; dynamic DO extension and REP handling; DSP self-write; actual DMA callback/bootstrap/VM sync/backend recreation; WAIT/fast interrupt sequencing; undefined behavior; tracing; cold/warm architectural state. External APU peripheral/frame callbacks are fail-fast test boundaries, not substituted dispatch code. The recreation test exercises the C-side VM handoff, not execution through the real JIT backend.

The unchanged parent fails the parallel-cache and reset-cache checks as expected. Independent semantic expectations pass the parent. Two deliberate negative controls fail their architectural-state assertions: omitted P-write invalidation and forced NOP dispatch. [Negative controls](negative-controls.json) and associated logs are retained.

The new target passes through the normal Meson test path: **10 subtests, 0 failures**. Expected production undefined-instruction text is retained; the TAP reader labels those five extra diagnostic lines `UNKNOWN` and ignores them. Standalone modified translation units compile with `-Wall -Werror`. A full local `qemu-system-i386` build succeeds. Existing third-party VMA/fpng/stb warnings remain in its log. The full local unit-suite command cannot finish building because unchanged `test-xbox-mcpx-apu-resampler.c:74` lacks declarations for `sinf`/`cosf` under GCC 14. The failure is retained; the whole suite is **not** claimed green. CI and native candidate qualification remain pending.

A review found that the benchmark driver could overwrite a prior output campaign. It now refuses an existing output directory before writing any file. [Preservation check](output-preservation-check.json) verifies a pre-existing manifest remains unchanged. No previous campaign was overwritten in this investigation.

## Reuse

Build `test-xbox-mcpx-dsp-dispatch` via the normal Meson unit target. CLI:

```sh
./build/tests/unit/test-xbox-mcpx-dsp-dispatch --benchmark mixed 5000000
python3 tests/unit/dsp-dispatch-benchmark.py PARENT_EXE CANDIDATE_EXE NEW_OUTPUT_DIRECTORY --cpu 0
```

The new output directory must not already exist. Modes are `normal`, `parallel`, `mixed`, `working-set`, `cold`, and `updates`. The tools and tests remain in the xemu draft; no evidence-only change is placed in the runner/test-suite repository.

## Decision

Keep this draft on hold. The typed cache is small and correctness checks support continuing, but compiler/workload regressions prevent a general performance recommendation. Collect the already-running diagnostic controls, classify their frame-evidence limitation, obtain matching product binaries, and qualify C-native A/A plus ABBA/BAAB with an unaffected JIT control before deciding whether the native benefit warrants further investment. Do not turn the roughly 3.36% sampled selection region or 7–30% synthetic gains into a game-speedup claim.

> Agent declaration: implementation, tests and evidence prepared with Codex (GPT-6).
