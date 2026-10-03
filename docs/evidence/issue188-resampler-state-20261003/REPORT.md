# Issue #188: finite resampler state and Deck component performance

## Decision

**PR #189 remains HOLD.** The linear converter is a real performance opportunity:
on the Steam Deck `.123`, the same fixed-work executable completed the production
VP workload **39.84% faster** with linear than with sinc. The actual PR #189 drain
path also consumed every source sample in all 600 finite-state cases, while
unchanged main did so in 440. This campaign does not replace native audio
acceptance, current-head game testing, save/load and starvation coverage, or the
required XISO comparisons.

## Quick comparison

| Check | Baseline | Candidate | Difference | Improvement | Correctness |
|---|---:|---:|---:|---:|---|
| Deck fixed-work VP time, 4 balanced samples | sinc **6,193,474.75 us** | linear **3,725,864.50 us** | **-2,467,610.25 us** | **+39.84%** | All 8 A/B samples passed; checksum 3,686,400,000 |
| Finite cases consuming the complete payload | main **440/600** | PR #189 **600/600** | **+160 cases** | N/A | Both completed the 600-case invariant suite; candidate also passed the strict finite-tail gate |
| Output duration delta from rounded ideal | main **-324..0 samples** | PR #189 **-1..+4 samples** | Candidate bounded near ideal | N/A | Linear's one-frame safety guard accounts for +1..+4 in 80 cases |

Positive improvement means less elapsed time:
`100 * (sinc_time - linear_time) / sinc_time`.

## What was tested

The retained state fixture links the actual VP implementation, QEMU RAM and
`AddressSpace`, PCM/ADPCM sample reader, libsamplerate callback, worker dispatch,
notifications, and mix stage. It covers:

- sinc and linear;
- mono/stereo S16 PCM and mono/stereo IMA ADPCM;
- 15 source lengths around 32/64/128/256-sample boundaries;
- five pitches giving output/input ratios of 4, 2, 1, 0.5, and 0.25;
- 600 total finite voices.

The candidate executable was built from PR #189 commit
`c36385e2381fdde9ff3f00a9a8aff9f409c979c5`. The reference was main commit
`76c23c7d444a6f12c9778bb2c35fab513f6c8056`. Both Deck runs completed with
`passed/complete/eligible` runner assessments. The main run is a characterizing
control and does not pass the opt-in strict finite-tail assertion; its trace shows
that completion inside the callback can discard the just-generated output block.

The throughput tool uses 45 looping mono ADPCM voices, eight VP workers, a
256-frame warmup, and 20,000 measured VP frames. A constructor-only test wrapper
selects sinc or linear in one executable; callback, fetch, resampler reads,
worker dispatch, and mixing remain production code. Its timed region has no
tracing or per-frame output, but does include identical per-frame checksum and
amplitude-oracle arithmetic for both converters; it measures the complete VP
fixture rather than isolated libsamplerate time. Looping voices intentionally exclude PR #189's
finite-drain change, so this measurement isolates the converter opportunity and
does not claim performance for the complete PR #189 runtime diff.

## Terms used in the state report

| Term | Meaning |
|---|---|
| `input_frames` | Source samples per channel. The guest EBO is this value minus one. |
| `payload_frames` | Real nonzero source samples returned by the callback in this constant-signal fixture. |
| `generated_frames` | Samples returned by libsamplerate, including samples later discarded or mixed as silence. |
| `mixed_frames` | Nonzero left-channel samples observed at output bin 0. This is valid as a duration proxy only for this fixture. |
| `duration_delta` | `mixed_frames - round(input_frames * output/input ratio)`, in samples per channel. |
| VP frame | One APU voice-processing block requesting 32 output samples per channel. |
| callback | One libsamplerate request for more source data; it is distinct from a VP frame. |

## Steam Deck ABBA/BAAB results

Every row used executable SHA-256
`c3cdff94f98541e877df769dfd79a909622b7d9cab054ba3753aeaebb8712b1d` and
the same libraries, workload, process target, and host. Times are the tool's
measured fixed-work region.

| Order | Position | Converter | Elapsed (us) | us / VP frame | Checksum | Runner result |
|---|---:|---|---:|---:|---:|---|
| ABBA | 1 | sinc | 6,130,907 | 306.545 | 3,686,400,000 | passed / complete / eligible |
| ABBA | 2 | linear | 3,584,132 | 179.207 | 3,686,400,000 | passed / complete / eligible |
| ABBA | 3 | linear | 3,841,320 | 192.066 | 3,686,400,000 | passed / complete / eligible |
| ABBA | 4 | sinc | 6,231,530 | 311.577 | 3,686,400,000 | passed / complete / eligible |
| BAAB | 1 | linear | 3,739,798 | 186.990 | 3,686,400,000 | passed / complete / eligible |
| BAAB | 2 | sinc | 6,217,633 | 310.882 | 3,686,400,000 | passed / complete / eligible |
| BAAB | 3 | sinc | 6,193,829 | 309.691 | 3,686,400,000 | passed / complete / eligible |
| BAAB | 4 | linear | 3,738,208 | 186.910 | 3,686,400,000 | passed / complete / eligible |

| Balanced group | Mean sinc (us) | Mean linear (us) | Difference (us) | Improvement |
|---|---:|---:|---:|---:|
| ABBA | 6,181,218.50 | 3,712,726.00 | -2,468,492.50 | +39.94% |
| BAAB | 6,205,731.00 | 3,739,003.00 | -2,466,728.00 | +39.75% |
| Combined | 6,193,474.75 | 3,725,864.50 | -2,467,610.25 | **+39.84%** |

The fresh sinc A/A samples were 6,229,583 and 6,165,407 us. Their range was
1.04% of their mean. ABBA and BAAB improvement estimates differ by 0.19
percentage points. Linear's four-sample range was 6.90% of its mean; retaining
every sample prevents the fastest observation from becoming the headline.

## Finite-state results

| Implementation | Invariant cases | Complete payload | Exact rounded duration | Duration-delta range |
|---|---:|---:|---:|---:|
| Main `76c23c7d44` | 600/600 | 440/600 | 8/600 | -324..0 |
| PR #189 `c36385e238` | 600/600 | 600/600 | 466/600 | -1..+4 |

Candidate duration deltas were: sinc `-1` for 54 cases and `0` for 246;
linear `0` for 220, `+1` for 40, `+2` for 20, and `+4` for 20. The 80 positive
linear cases are the documented one-real-frame safety guard at finite EOF. This
is a bounded behavioral tradeoff that still needs native listening acceptance.

## Test and failure ledger

- Local GCC 14 optimized build: retained resampler unit suite passed 10 tests;
  the state fixture passed eight groups / 600 cases; the throughput fixture
  passed six functional subtests.
- The explicit silence negative control aborted, proving the state fixture does
  not pass when output is suppressed.
- The explicit one-frame linear libsamplerate diagnostic aborted with the known
  channel mismatch; the production-compatible 32-frame callback test passed.
- Main's strict finite-tail negative control failed as designed; PR #189 passed
  that same gate for all 600 cases.
- One Deck request, `i188-unit-meson-20261003-001-t001`, selected the state
  executable for a unit-test contract. It completed but was correctly assessed
  `correctness=failed`, `evidence=incomplete`, and `comparison=ineligible`.
  The corrected request `i188-unit-meson-correct-20261003-001-t001` passed all
  10 tests. The failed attempt is retained in the raw evidence.

## Identities and raw evidence

| Item | Identity |
|---|---|
| Deck | Steam Deck `.123`; AMD Custom APU 0405; SteamOS; 8 logical processors |
| Runner | `0.2.0+e17919c849b840d43171a615b288f559fbec9e4c` |
| State reference executable | `a0364655f2a5bbedac85f8f29dd1312537f5ee2148c0fe1f342a60c8ab504c98` |
| State candidate executable | `5d483ecc91368086ba70419ad2d94aba61245044aa4cec84da86e031005b49fd` |
| Throughput executable | `c3cdff94f98541e877df769dfd79a909622b7d9cab054ba3753aeaebb8712b1d` |
| libsamplerate | 0.2.2 |
| Retained fixture source | `d9d56e2f2d02ac7a0b6a7fb5e90df2531dbb57fd` |

`SUMMARY.json` is the machine-readable compact result. `RUNS.csv` maps every
balanced sample to its immutable request and run. `RAW-EVIDENCE.tar.gz` retains
the complete main/candidate state traces, local checks and negative controls,
runner assessments/manifests/stdout for the final state, corrected/failed unit,
A/A, and all eight balanced throughput runs. Large executables are excluded;
their hashes and package manifests are retained.

## Limits and bounded handoff

This is a Linux component campaign. It contains no game, renderer, DSP program,
XISO, guest clock, save/load, native audio capture, or bare-metal Xbox result.
The checksum proves fixed work within the fixture, not equivalent sinc/linear
waveforms or audio quality. Before PR #189 can move from HOLD, run current-head
native audio acceptance, starvation/recovery and lifecycle checks, required
XISO correctness/timings and a directly verified PGR2 scene comparison. Preserve
linear as opt-in until those gates establish the tradeoff is acceptable.
