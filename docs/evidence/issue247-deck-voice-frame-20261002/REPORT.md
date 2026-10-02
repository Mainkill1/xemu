# Equal voice store performance on Steam Deck

**Decision: HOLD.** This report evaluates the existing RAM equal-store
candidate in [xemu draft PR276](https://github.com/Mainkill1/xemu/pull/276),
issue #247. It extends the earlier unchanged-store attribution and capped
Windows game measurement with actual production VP work on the Deck.
Required guest ordering, audio, lifecycle and XISO qualification remain open.

## Baseline and candidate

Each process performs **20,000 voice-processing frames, 45 voices and 32
samples per voice per frame**. The numbers are host elapsed seconds for this
fixed batch, including identical output validation. They are not game frame
times or FPS. Mono and stereo ADPCM use encoded samples; PCM uses uncompressed
samples. All three formats exercise the changed store helper. Worker threads
are the actual production VP workers, using either one or eight threads.

**A** is the original physical-store implementation; **B** is the candidate
that validates an unchanged RAM word using a same-value compare-exchange.
The saved runner tests physically execute four original A/A controls, then
**A B B A**, then **B A A B**, for each row. Main medians use the four A and
four B processes in those balanced orders. A/A controls are separate.
Positive Improvement % means less elapsed time:
`100*(baseline-candidate)/baseline`.

| Voice format | Worker threads | Baseline s | Candidate s | Saved s | Improvement % |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mono ADPCM | 1 | 5.598410 | 5.379434 | +0.218976 | +3.91% |
| Stereo ADPCM | 1 | 6.619660 | 6.407072 | +0.212588 | +3.21% |
| Mono PCM | 1 | 6.174472 | 5.922336 | +0.252136 | +4.08% |
| Mono ADPCM | 8 | 5.850624 | 5.651250 | +0.199374 | +3.41% |
| Stereo ADPCM | 8 | 6.839053 | 6.651581 | +0.187472 | +2.74% |
| Mono PCM | 8 | 6.393477 | 6.224968 | +0.168509 | +2.64% |

24 of 24 adjacent A/B or B/A pairs use less time for the candidate.
Use the order and variation table below when interpreting small differences.
This small campaign provides no confidence interval or general game speedup
claim. [SUMMARY.json](SUMMARY.json) retains every range and pair;
no attempt was omitted or replaced.

## Variation and execution order

| Voice format | Worker threads | Baseline A/A spread | ABBA improvement | BAAB improvement | Four adjacent pair improvements |
| --- | ---: | ---: | ---: | ---: | --- |
| Mono ADPCM | 1 | 1.54% | +4.25% | +4.00% | +3.36%, +5.13%, +4.55%, +3.45% |
| Stereo ADPCM | 1 | 0.11% | +3.40% | +2.80% | +3.64%, +3.16%, +2.53%, +3.07% |
| Mono PCM | 1 | 1.85% | +5.64% | +3.09% | +4.80%, +6.46%, +3.12%, +3.07% |
| Mono ADPCM | 8 | 0.80% | +3.43% | +3.23% | +3.40%, +3.46%, +3.17%, +3.29% |
| Stereo ADPCM | 8 | 0.54% | +2.74% | +2.63% | +2.90%, +2.59%, +2.59%, +2.66% |
| Mono PCM | 8 | 1.17% | +2.38% | +2.38% | +1.78%, +2.98%, +2.84%, +1.92% |

A/A spread is `(maximum-minimum)/median` for the four original controls.
No CPU affinity or power policy was changed or pinned. The first 256 frames
and one qualified frame precede timing. Sinc startup is excluded, while each
timed frame checks all 1,024 output samples and computes 64 checksum
contributions. That common validation cost can dilute the relative saving.

## What work was removed

Store counts come from **12 separate diagnostic processes**, not the timing
executables. The wrapper forwards every physical store to the real QEMU
implementation. Diagnostic elapsed values are retained but are
**diagnostic-only / NOT COMPARABLE** as performance measurements.

| Voice format | Worker threads | Baseline physical stores | Candidate physical stores | Unchanged physical stores removed |
| --- | ---: | ---: | ---: | ---: |
| Mono ADPCM | 1 | 5,400,000 | 900,000 | 4,500,000 |
| Stereo ADPCM | 1 | 5,400,000 | 900,000 | 4,500,000 |
| Mono PCM | 1 | 5,400,000 | 900,000 | 4,500,000 |
| Mono ADPCM | 8 | 5,400,000 | 900,000 | 4,500,000 |
| Stereo ADPCM | 8 | 5,400,000 | 900,000 | 4,500,000 |
| Mono PCM | 8 | 5,400,000 | 900,000 | 4,500,000 |

Every profile keeps exactly 900,000 changed physical stores, one per voice
per frame, and removes the 4,500,000 unchanged physical stores observed in
the original. The candidate still reads and translates RAM and executes an
atomic same-value compare-exchange; the count reduction does not mean those
updates become free. The ordinary timing executable has no store-count code.

This connects #270's earlier roughly 88% unchanged-store observation to the
actual candidate mechanism. This fixture's unchanged fraction is 83.33%.
It does not combine the #197 sample-reader optimization with #247, and its
elapsed values must not be pooled with #197's separate processing campaign.

## Builds and native execution

- Original production commit: `ee5ce48b48784f999af374c1452003f8b2b1230f`.
- Reference fixture commit: `dd1a8f48690803975d9e97b8e1139dcc4164fc83`.
  It adds only the identical fixture, documentation and Meson/CI registration.
- Candidate input: `5d832c02c3ed6e1f56ef1b808fe74d8f3ea565ee`.
  The production algorithm is unchanged from prior reviewed `c4d36ecfa3`.
- CI [reference 37038506712](https://github.com/Mainkill1/xemu/actions/runs/37038506712)
  and [candidate 37038440974](https://github.com/Mainkill1/xemu/actions/runs/37038440974)
  both complete successfully. Both Linux release builds pass six ordinary
  and six diagnostic fixture cases.
- Source archives match the exact fixture and production inputs. Clang/LLD
  **21.1.8**, release ThinLTO, x86 version 3 and all **38 corresponding runtime
  libraries** match. All 42 executable/library files are explicitly declared
  as replaceable build inputs before the first run.
- Ordinary executable SHA-256: baseline `7adfaf350bf05b0552442040f659ef04d4e98455feb4db4b50bef954c1abb7f8`;
  candidate `ad727eb7888007d2a99e05bfd73961ce23da65bab51bcf71fb9d8d09df29b9d8`. Diagnostic hashes, full compiler logs
  and every library hash are in the archive.
- Native host: **Steam Deck 10.0.0.123**, HTTP port **9368**, maintained
  runner revision **f5b3e58**. Upload, execution and collection use HTTP.
  This campaign does not use the Windows box or SSH for measurements.

The fixture owns 2 MiB of system RAM mapped at zero; voice data begins above
1 MiB. It enables all three NV2A dirty clients. No renderer clears or consumes
their dirty bitmaps, and no vCPU/TCG code executes. The work therefore has no
Mesa cache or shader compilation. It does not qualify or repair the earlier
game campaign's uncontrolled Mesa cache and input-start failures.

## Correctness and retained tools

All **88 native attempts** exit zero, report completed/passed/complete/eligible,
and have complete artifact collection with zero excluded files. Four TAP
processes pass six cases each. The 72 ordinary timing and 12 diagnostic
processes match checksum **3,686,400,000**, independently expected signed
mixes, the 32-unit signed-24-bit error bound, exactly 45 dispatched and active
voices, independently expected final buffer offsets, and poisoned SGE gaps.
Worker and SRC finalization complete in every process.

The output signal is constant, so equal offsets across voices alone would
miss uniformly dropped changed writes. A read-only review found that gap.
The retained fixture now separately runs a libsamplerate consumption oracle
outside timing; it never reads VP state or invokes the store helper. Dropping
changed stores makes the ordinary fixture abort at offset 0 versus independently
expected 3392 in the local 1,000-frame negative. The repeated negative script
passes. Deliberate monitor silence also aborts. This covers those mutations,
not general sample ordering or concurrent guest writers.

Local policy 3/3, real-memory 1/1, ordinary VP 6/6 and diagnostic VP 6/6 checks
pass. The attempted full local unit build fails in unchanged resampler-test
source because GCC 14 rejects missing `sinf`/`cosf` declarations. That failure
is retained; the full local suite is not claimed green. The initial unadapted
#197 fixture crashed on this branch because it omitted the APU RAM owner;
the adapted fixture initializes that owner. This was a fixture setup failure.
An initial negative-link command found no retained linker response file;
the repeatable script explicitly retains it. An initial format preflight
also failed and was corrected before the matched CI builds. These setup
failures precede native execution and caused no native rerun.

Both executables and their documentation remain in `tests/xbox/mcpx-apu`;
Linux CI builds, verifies and retains them. Twenty-eight saved profile,
worker, build and mode definitions remain installed on the Deck with pinned
revisions. Frozen requests, all launch and host records, paginated server
results, full comparison CSV, negative controls and reusable orchestration
scripts are in [EVIDENCE.tar.gz](EVIDENCE.tar.gz).

Independent verification matches every ordinary raw elapsed value to its
indexed metric and checks equal A/B workload/environment keys in all six
cohorts. The server comparison pools eight A processes, including controls,
against four B processes; the opening table keeps the balanced four/four
medians. Untimed TAP and diagnostic records remain separate.

All 72 timing records are index-eligible. The 16 correctness/count records
deliberately declare no measurement metric and appear as `metrics_missing`
in the performance index. They are not failed timing attempts. The first
verification script incorrectly required metrics for those untimed records;
its assertion failure and corrected reparse are retained. No native attempt
was rerun to correct that verifier. Full server output includes the ineligible
untimed TAP row; all six `validated-vp-frame-time` rows are comparable.

[INDEX.json](INDEX.json) lists every payload hash and large-binary omission.
Archive SHA-256: `ae7b679fc1c0b5031d89ea4249a8eff8156fac88df9045c9046ab2db1ecd1efc`.

## Earlier evidence and remaining work

The earlier Windows Vulkan PGR2 campaign remains separate: **318.428 to
311.444 core percent**, 2.19% less sampled process CPU, at a 30 FPS cap;
p95/p99 were slightly worse. Its original eight runs and failed/ineligible
attempts remain in the [atomic revision report](../../../evidence/wiki-xiso-per-test/pr-276-issue247/ATOMIC-REVISION.md).
No new Windows game or Deck FPS result is claimed here.

The #247 candidate still needs the complete guest-writer and mapping ordering
contract, sample-order/affected audio validation, reset/save-load coverage,
and the maintained APU XISO fixture with approved references and usable
measurements. The [XISO fixture draft #52](https://github.com/Mainkill1/xemu-perf-tests/pull/52)
has useful affected leaves but remains unqualified for this PR. This synthetic
steady-state workload cannot establish full audio/DSP scheduling, hardware
notifications, audible playback or renderer interference. Keep PR276 draft
and HOLD until those remaining gates are satisfied.
