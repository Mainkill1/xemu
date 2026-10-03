# #250 finish attribution: retain measurement gate, defer ownership prototype

## Decision

**HOLD.** This branch adds a maintained offline finish-reason analysis mode and unit registration. No Vulkan submission, resource lifetime or guest behavior changes. Reused Deck traces establish a bounded buffer-pressure signal, not a baseline/candidate improvement. Do not build a deferred-submission architecture from these limited captures. Keep #250 open for representative pressure/critical-path attribution.

## What exists on this branch

`scripts/performance/vk-perf-summary.py` retains the CPU-region/auxiliary reader from [PR #293](https://github.com/Mainkill1/xemu/pull/293), source `d1970118f30`, and adds `--finish`. `tests/unit/test-vk-perf-summary.py` carries 15 original checks and nine new finish/CLI/compatibility checks; `tests/unit/meson.build` registers them. `docs/performance/vulkan-finish-attribution.md` documents actual units, sampling and boundaries. Source is based on current main `76c23c7d444a6f12c9778bb2c35fab513f6c8056`; no dependency on the texture collector runtime is introduced.

The reader reports all ten production reason names, calls/submissions/samples/fence waits, measured sampled host elapsed submit/wait milliseconds and timing coverage. It never scales sparse samples, sums nested CPU regions or predicts FPS/gains. The current policy samples first eight **calls** per reason/control frame, then every sixteenth call; non-submitting calls still advance the ordinal. Hybrid tracing may time additional submissions, so recorded samples determine coverage. Missing/malformed contracts fail closed. New report output cannot overwrite existing evidence; hashing streams 1 MiB chunks using supported Python 3.9 APIs.

```text
Existing complete schema-8 log → unchanged CPU/auxiliary analyses
                            → new finish analysis:
                              exact counts + sampled duration coverage
                              all buckets; complete tail; separate crossing bucket
Emulator record/submit/wait/retire ownership → unchanged
```

## Results: reused diagnostic captures

| Finish reason | PGR2 calls / submits / timed | PGR2 sampled wait ms | PGR2 complete-tail submits / wait ms | Morrowind calls / submits / timed | Morrowind sampled wait ms | Morrowind complete-tail submits / wait ms |
|---|---:|---:|---:|---:|---:|---:|
| vertex_buffer_dirty | 0 / 0 / 0 | 0.000 | 0 / 0.000 | 0 / 0 / 0 | 0.000 | 0 / 0.000 |
| surface_create | 93 / 77 / 36 | 13.229 | 0 / 0.000 | 28 / 18 / 15 | 7.649 | 0 / 0.000 |
| surface_down | 108 / 41 / 41 | 153.958 | 0 / 0.000 | 28 / 12 / 12 | 50.096 | 0 / 0.000 |
| need_buffer_space | 296 / 296 / 296 | 1372.879 | 133 / 363.624 | 0 / 0 / 0 | 0.000 | 0 / 0.000 |
| framebuffer_dirty | 0 / 0 / 0 | 0.000 | 0 / 0.000 | 0 / 0 / 0 | 0.000 | 0 / 0.000 |
| presenting | 3419 / 3419 / 3419 | 2444.835 | 81 / 44.965 | 363 / 363 / 363 | 310.821 | 0 / 0.000 |
| flip_stall | 3704 / 2434 / 2434 | 4399.346 | 79 / 85.313 | 7550 / 7468 / 7468 | 4020.052 | 1547 / 904.288 |
| flush | 1 / 0 / 0 | 0.000 | 0 / 0.000 | 1 / 0 / 0 | 0.000 | 0 / 0.000 |
| stalled | 100 / 100 / 100 | 150.845 | 81 / 101.750 | 1751 / 1751 / 1751 | 3438.079 | 1547 / 2658.743 |
| texture_dirty | 0 / 0 / 0 | 0.000 | 0 / 0.000 | 0 / 0 / 0 | 0.000 | 0 / 0.000 |

**These are different workloads, not A versus B.** No candidate time, saved time or Improvement % exists. Counts are exact within recorded buckets. Durations are measured **host elapsed time**, not CPU execution, GPU work or avoidable cost. `surface_create` has partial timing coverage (PGR2 36/77 submissions, Morrowind 15/18); its durations omit untimed waits. The nonzero buffer-space, presentation, flip-stall, report-stalled and surface-down rows are fully timed within this recorded scope. Do not add auxiliary or nested region totals to these values.

PGR2 records 296 `need_buffer_space` submissions and 1,372.879 ms of corresponding fence wait across the whole log. Complete final-60-second buckets contain 133 submissions/363.624 ms; the crossing bucket separately contains two submissions/6.278 ms and cannot locate those events within the tail. `vertex_buffer_dirty` is zero. This is a measured signal for bounded follow-up, not evidence that removing the wait will save equivalent useful-work time or improve tails.

Morrowind has no buffer-space or vertex-dirty submits, including its boundary bucket. Its steady waits are flip/report retirement. Those remain guest-observable/excluded from #250's initial deferral target; do not relabel them non-observing. Neither scene establishes broad architecture value.

## Source, workload and run identities

- Offline reader source commit `3e69f0b8f11`; final 24 tests pass after compatibility repair. Both native outputs were recomputed with final source and match the pre-repair summaries exactly.
- Native host **Steam Deck 10.0.0.123**, Vulkan/full DSP/default JIT, historical maintained HTTP runner `848dca74e1ff79f9fc886769a785c23a0945a87e`. **Zero new native attempts**. Deck now runs `e17919c`; these older diagnostic outcomes remain unchanged.
- Measured collector runtime `8079502ae25a602e1395fa9ee87ec1ef8b57c9d6`; executable SHA-256 `1b52adf0dea0823a95c37ae79d447af01bf8a904d6385037908b2ca57d44bba2`; original pinned parent library bundle. Candidate-packaged GLib differed; original procedure used the matched parent bundle. The actual `draw.c` finish path and `perf.c` are byte-identical between measured runtime and current main. Whole `renderer.h` differs because of collector metadata; no whole-header/binary equivalence is claimed.
- PGR2 run `20261002-063023515-b3b4572c8e90494aa88319ceed4e7c9c`, 3,704 control-frame records, timestamp span 163.876093 s; raw SHA-256 `13e6f1c82d24da90526832515d3dbb11656382aa38620ce6ee7d96978181b336`.
- Morrowind run `20261002-062420291-70415e0a4aba429ba9a0001eaebb2493`, 7,550 control-frame records, timestamp span 231.077884 s; raw SHA-256 `f13a45b70a1c1a638017167de1b8fce64fd484f5ed253cb46d271a70bda363e8`.
- PGR2 is the parked Hong Kong starting grid at 0 MPH; Morrowind is the ship interior/NPC behind name entry. Source scene audits and original captures are retained. No sustained driving/streaming, repeated transitions or open-world workload is qualified. Historical PGR2 is under 300 seconds and cannot satisfy the current five-minute performance gate.
- Both original diagnostic procedures completed and passed their declared visibility/evidence contracts and private cold Mesa disk qualification without waivers. These limited generic assessments do not qualify an emulator optimization, audio parity or equivalent guest work. PGR2 allocation collector was ON; Morrowind was OFF. Do not treat the two workloads as an observer-overhead comparison. OS page cache, driver memory cache, host scheduling/power remain limitations.

## Boundaries and validation

Per-control-frame arrays describe buckets ending at record timestamps. Their individual events cannot be placed within a bucket. First bucket start and work after final record are unobserved. Tail totals use only complete buckets: **81 PGR2 / 1,547 Morrowind**, with crossing buckets reported separately. Frame labels are not display FPS. Tail memory is bounded by time span, not a fixed record count.

Fifteen baseline checks pass. New finish behavior reproduces RED before implementation. Final **24 reader tests pass**; test input over the runner's 16 MiB text limit remains supported. Two intentional faults (doubling sampled durations and crediting the crossing bucket) fail their expected behavioral assertions. Read-only review caught inherited `hashlib.file_digest` use unavailable on Python 3.9/3.10; a missing-API CLI regression reproduces RED, then passes with streamed hashing. Independent review reruns all 24 checks and reports no remaining blocker. This simulates the missing API on installed Python; it is not an actual Python 3.9 interpreter run.

No renderer/emulator rebuild or XISO A/A/ABBA/BAAB was needed for this offline source change; emulator performance is N/A because runtime behavior is unchanged. Meson registration will be exercised by branch CI; CI status belongs in the canonical PR body. There is no full-unit/emulator success claim. Earlier collector profiler timeouts, PFIFO abort and rejected driving attempts remain in [PR293 native evidence](https://github.com/Mainkill1/xemu/blob/research/issue246-allocation-attribution/docs/evidence/issue246-collector-native-20261002/README.md) and [follow-up](https://github.com/Mainkill1/xemu/blob/research/issue246-allocation-attribution/docs/evidence/issue246-collector-followup-20261002/README.md); this report never repairs or excludes them.

## Next gate

Collect representative >=300-second reached PGR2 pressure, the pinned Morrowind workload, high-pressure/readback and low-pressure controls. Classify next completion consumer, resource/descriptor/buffer generation ownership, report/readback overlap, per-call tails and total critical-path wait. Existing aggregate reason names alone cannot establish those facts. Only material non-observing waits justify an independently measured slot/ownership prototype, followed by fresh A/A, uninstrumented ABBA/BAAB and production-linked lifetime/output tests. No wait is removed, tolerance enlarged, guest clock changed or interrupt forced in this branch.

[Complete summaries and source/run bindings](SUMMARY.json) · [Original byte-exact logs, audits and checks](EVIDENCE.zip) · [Public payload hashes](INDEX.json). All emulator evidence remains in xemu; no evidence-only runner/test-suite commits.
