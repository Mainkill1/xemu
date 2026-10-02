# Matched production voice-frame comparison on Steam Deck

**Decision: HOLD.** This packet tests the performance scope of
[xemu draft PR275](https://github.com/Mainkill1/xemu/pull/275), issue #197.
It measures actual production voice processing, decoding, sinc SRC, mixing
and worker dispatch plus identical output validation. It is synthetic fixed
work, not game FPS, full DSP, audio pacing or audible playback.

## Baseline versus candidate

Each number is host monotonic time for **20,000 frames of 45 voices**,
32 samples per voice per frame. Each process excludes 256 startup frames
and one verified frame. Primary medians use **four original and four candidate
processes**, physical **ABBA then BAAB** through pinned saved tests with
all forty build slots explicit. The preceding four original A/A
controls are separate and not pooled into these medians.
Positive Improvement % means less time: `100*(original-candidate)/original`.

| Profile | Workers | Original s | Candidate s | Saved s | Improvement % | Correctness |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| mono-adpcm | 1 | 5.432154 | 5.359669 | +0.072484 | +1.33% | PASS |
| stereo-adpcm | 1 | 6.373821 | 6.166621 | +0.207200 | +3.25% | PASS |
| mono-pcm | 1 | 5.905593 | 5.961302 | -0.055709 | -0.94% | PASS |
| mono-adpcm | 8 | 5.634140 | 5.531800 | +0.102341 | +1.82% | PASS |
| stereo-adpcm | 8 | 6.627281 | 6.365266 | +0.262014 | +3.95% | PASS |
| mono-pcm | 8 | 6.150617 | 6.165291 | -0.014674 | -0.24% | PASS |

Mono/stereo ADPCM exercise the changed reader; mono PCM is the unaffected
control. This directly extends the earlier **48.04% / 54.35% encoded-reader
component reductions** to a larger processing boundary. The earlier component
did not include this decoder/SRC/mixer/worker work and cannot predict these
percentages or game FPS. See the existing component and inconclusive parked
PGR2 packets linked in PR275. All sixteen ADPCM adjacent pairs use less time,
but mono with one worker varies between +2.45% and +0.38% by order. Both PCM
medians are slower: one-worker orders reverse sign, and all four eight-worker
pairs are slightly slower. The small PCM differences are near or below A/A
variation; they remain disclosed costs, not automatically dismissed as noise.
Keep the small/mixed results descriptive; this
campaign provides no confidence interval or general game-speedup proof.

## Variation and order

| Profile | Workers | A/A spread | ABBA improvement | BAAB improvement | Four adjacent-pair improvements |
| --- | ---: | ---: | ---: | ---: | --- |
| mono-adpcm | 1 | 0.79% | +2.45% | +0.38% | +2.39%, +2.51%, +0.48%, +0.28% |
| stereo-adpcm | 1 | 0.57% | +3.25% | +3.33% | +3.45%, +3.05%, +4.48%, +2.16% |
| mono-pcm | 1 | 1.32% | +0.14% | -1.91% | -0.78%, +1.06%, -1.26%, -2.56% |
| mono-adpcm | 8 | 0.16% | +1.86% | +1.67% | +1.81%, +1.90%, +1.44%, +1.91% |
| stereo-adpcm | 8 | 0.32% | +4.02% | +3.86% | +3.92%, +4.12%, +3.47%, +4.24% |
| mono-pcm | 8 | 2.39% | -0.47% | -0.12% | -0.45%, -0.49%, -0.21%, -0.02% |

A/A spread is `(maximum-minimum)/median`; individual ranges and within-build
balanced spreads are in [SUMMARY.json](SUMMARY.json). Four pairs are the
adjacent A/B or B/A processes in ABBA and BAAB. No host power policy or
CPU affinity was pinned. All repetitions, controls and regressions remain
visible; none was dropped or replaced. The included per-frame oracle checks
1,024 output samples and calculates 64 checksum contributions, so its common
cost can dilute the percentage of the production work itself.

## Source, builds and execution

- Fixed original production: `ee5ce48b48784f999af374c1452003f8b2b1230f`.
- Reference harness input: `3bc242754a6100b71495bb9dfcd241f3f10b24b9`;
  only the identical fixture, documentation and build/CI registration were added.
- Candidate input: `fa3146586c6ca242ec707ba00a012c4b0e0fbee5`.
  Production reader/caller remain identical to the prior measured `e25b0ba`.
- CI [reference 37028671968](https://github.com/Mainkill1/xemu/actions/runs/37028671968)
  and [candidate 37028558983](https://github.com/Mainkill1/xemu/actions/runs/37028558983)
  report workflow success (reference: 20/20 jobs reported success; candidate: 20/20 jobs reported success). The earlier pending Windows PDB
  snapshot is retained. Both release builds pass all six retained VP cases. Candidate
  production original/candidate memory modes each pass thirteen cases.
- Identical Clang/LLD **21.1.8**, release ThinLTO, x86 version 3 and all
  **38 corresponding runtime libraries**. Source archives match the exact
  fixture, VP/decoder/SRC and build inputs. Qualification receipts, compiler
  logs and source copies are inside the archive.
- Executable SHA-256 original `f6b1e664fda3d11d07935e89db6acefc0ecba736de8679c7d355d0311c70bf04`;
  candidate `e9b055f095eaf41af6e70fd1eda01f160331f9f00880bb2ea2880555ba25cd6d`.
- Deck **10.0.0.123**, maintained HTTP runner **f5b3e58**, port 9368.
  All upload, execution and collection use HTTP. No Windows or SSH measurement.
  One immutable forty-file bundle contains both builds and shared libraries.
  No renderer, Mesa cache, QMP, input replay, timer throttle or guest clock is used.

## Correctness and retained tooling

Both native correctness processes pass **6/6**; all **72 corrected timing processes**
exit zero and report completed/passed/complete/eligible. Every timed run
checks checksum **3,686,400,000**, every output bin against the independently
expected `45/512` signed mix, error at most 32 signed-24-bit units, exact
45-voice dispatch, active voice states, common final buffer offset, and poisoned
physical SGE gaps. Worker and SRC finalization complete. Original/candidate
final offsets agree within every profile/worker cohort.

Local six-case checks and the four focused Meson targets pass. Deliberate
VP-monitor silence aborts with checksum zero instead of 184320. Invalid CLI
frames/profile/worker arguments exit two. This constant-signal oracle does
not prove sample ordering; the separate ramp/remap/lifetime tests provide
that coverage. Previously published broader local failures remain unresolved
and are retained in the lifetime packet; this focused run does not erase them.

The executable and usage contract remain in `tests/xbox/mcpx-apu`; Meson and
Linux CI retain it. Twelve reusable profile/worker/build tests are installed
on the Deck, each declaring all forty replacement build slots. Scripts,
frozen plans, indexed server measurements, comparisons, native launch/host
records and all **146 complete collections** are in [EVIDENCE.tar.gz](EVIDENCE.tar.gz).
[INDEX.json](INDEX.json) lists every payload hash and large-binary omission;
omitted binaries are retained locally and by CI/runner identities. Archive
SHA-256: `d7f00cb2ae2e21487ad9e0b1045c8fd22a1201e58610243c9fe35cad90e51be7`.

## Preserved initial campaign and reporting correction

The initial two correctness and 72 timing processes all completed and passed;
their measured values are in
[INITIAL-DIAGNOSTIC-SUMMARY.json](INITIAL-DIAGNOSTIC-SUMMARY.json).
Direct jobs declared no explicit build replacement slots. Default hash
indexing excluded only the running executable, treating the unused companion
executable as fixed workload data. A/B therefore had distinct canonical
workload keys: the server correctly reported missing A/B rows,
**NOT COMPARABLE**. Those values do not establish an eligible server comparison.

The corrected campaign uses the twelve retained saved tests, each declaring
both executables and all 38 libraries as build slots. No product, binary,
clock, oracle, frame count, worker count or settings changed. It runs a fresh,
explicitly preserved A/A + ABBA + BAAB campaign; no failed or incomparable
attempt was overwritten. Independent verification requires identical A/B
canonical workload/environment keys in each corrected cohort and matches every
raw time to its indexed reported metric. The server's combined history still
includes incomparable initial rows and untimed TAP controls. Its matched
corrected cohorts pool eight original processes (including four controls)
against four candidate processes; the primary table keeps the balanced
four/four subset separate. Full server output is retained.

An earlier source-qualification script used the wrong Meson path; its failed
preflight and correction are retained. This happened before native execution
and required no build or measurement rerun.

## Remaining limits

Required guest XISO timings remain **NOT COMPARABLE**: their sample-progress
polling is timer paced, and newly added framebuffer references are not yet
approved. Passing guest mix checks are correctness evidence. Audible playback
parity and repeatable whole-game/resource qualification remain open. The
previous PGR2 balanced result was inconclusive. This packet does not make
PR275 ready or authorize a merge.
