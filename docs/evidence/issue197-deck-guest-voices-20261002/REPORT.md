# Issue #197: retained Deck guest voice qualification

**Decision: HOLD.** Final original and candidate each pass all five generated-input GP-mix assertions and cleanup checks. A deliberately silent mono run fails the unchanged output oracle while engine progress and cleanup pass. These narrow results complement the component reader checks; they do not qualify audible output, framebuffer references or game speedup.

## What was tested

Steam Deck `10.0.0.123`, maintained HTTP runner `f5b3e58`, Vulkan, full DSP for positives / VP monitor for the negative, DSP JIT enabled, 128 MiB. Actual nonstreaming guest voice engine → SGE fetch → ADPCM/PCM → resampling → GP mix. Voice 64, fixed generated input, no retail assets or DSP firmware. The processors remain disabled; this observes the voice-stage GP mix, not an audible DSP output chain.

A is unchanged `ee5ce48`; B is candidate `c4cdef6`. Executables SHA-256 `5b6ccf357cfab428e92692b82dc3a5cc75cb50e4065608defbf1cc66b68a597c` / `95f81f33d932cb297d6f637eb0635ac2faa2b895d330bae6871e3012d6353667`; 38 matching libraries and release/compiler settings are bound by the retained identity. Final fixture source `850f5ac1e7905558e9f48179608c1b858704b3d3`, [genuine draft test PR #52](https://github.com/Mainkill1/xemu-perf-tests/pull/52). Final ISO SHA-256 `182013b490ac64d3dcee08cc77cc149e7f76bbacf2a182a928ef2509b11b37b7`; embedded catalog `sha256:4279bbcaf241ad2955c407bb6da50c4bb6131ed597a23121a50b147001e5c9cf`, 164 leaves/five groups. Old suite images were not relabeled.

Five selected leaves, warmup 0, multiplier 1, per_iteration, one process per build. Mono/stereo crossing leaves replay the first 64-sample block at byte 4080 over noncontiguous pages; all observed output depends on the crossing. Generated predictors ±4096 imply mix ±1048576 with declared 32-unit tolerance; zero headroom is programmed explicitly. XGSCNT checks ≥1024 engine periods, not fetch counts. Cleanup unlinks before the frame fence and restores prior stopped-engine/table/GP registers; two idle headroom settings remain zero.

## Quick original versus candidate results

| Test ID | Original guest | Candidate guest | Largest mix error A / B (24-bit units) | Exact 64-word parity | Performance |
| --- | --- | --- | ---: | --- | --- |
| `mcpx_voice.mono_aligned` | PASS | PASS | 1 / 1 | Identical | NOT COMPARABLE |
| `mcpx_voice.mono_page_crossing` | PASS | PASS | 1 / 1 | Identical | NOT COMPARABLE |
| `mcpx_voice.pcm_control` | PASS | PASS | 1 / 1 | Identical | NOT COMPARABLE |
| `mcpx_voice.stereo_aligned` | PASS | PASS | 1 / 1 | Identical | NOT COMPARABLE |
| `mcpx_voice.stereo_page_crossing` | PASS | PASS | 1 / 1 | Identical | NOT COMPARABLE |

All ten positive leaves have engine progress ≥1024, cleanup PASS, intact poison guards and independently generated input hashes. Both raw mix arrays are identical for each leaf. The mono VP-monitor negative has ≥1024 engine progress, all 64 mix words zero, cleanup PASS and expected guest FAIL. Host contracts separately reject stereo swaps and tolerance excess.

**The runner outcomes remain `completed / failed / complete / ineligible`.** No pinned framebuffer oracle exists for these new leaves: original/candidate canonical correctness fails that coverage check, even though their guest assertions pass. We preserve those outcomes and every failed record; no reference was self-approved or replaced. Exact selection receipts/extraction pass. These are unpaired correctness diagnostics, not an ABBA/BAAB timing campaign.

## Timing contract

Every leaf explicitly reports `timing_comparable:false`. Actual waits remain in raw results and full-precision summary; comparing their percentages would fabricate a performance result. They include progress polling, changing engine phase/pacing, and one sample. Use the separate 10M-block native reader ABBA/BAAB for component cost; game ABBA/BAAB remains inconclusive. No native wait improvement metric is calculated.

## Failure history preserved

| Stage | Native result | What changed next |
| --- | --- | --- |
| v1 | Five setup refusals, zero measured samples | Added read-only PCI/APU state diagnostics |
| v2 | Mono setup refusal; mapped PCI APU, SECTL 7, GP 0, EP 1, empty lists, nonzero BIOS tables | Accepted stopped empty-list state and restored old tables; still reject active/linked work |
| v3 | Five mix failures: progress/cleanup pass, output half declared amplitude | Programmed zero headroom; mathematical oracle unchanged |
| v4 original / candidate | Five guest PASS each; missing framebuffer references remain canonical failures | Retain narrow pipeline qualification |
| v4 VP negative | Expected silent-output FAIL; progress/cleanup pass | Oracle rejects silent output |

Client preparation also retains the wrong JSON-key error and invalid overlong campaign ID errors. Those requests launched no native process. No failed native attempt was rerun under the same identity or hidden. Each revised fixture has a distinct ISO hash and immutable suite.

## Every attempt

| Stage | Run ID | Collected files | Guest result | Canonical result |
| --- | --- | ---: | --- | --- |
| v1 original | `20261002-141304612-c29e20bcde1d4fea96b2b6406d080dfe` | 309 | 0 PASS / 5 FAIL | completed / failed / complete / ineligible |
| v2 original | `20261002-142012541-7939c645573d4ab092c404d21cc332b9` | 304 | 0 PASS / 1 FAIL | completed / failed / complete / ineligible |
| v3 original | `20261002-142817003-d878f076d888453c94a390f6b0151531` | 309 | 0 PASS / 5 FAIL | completed / failed / complete / ineligible |
| v4 original | `20261002-143432085-1d3f338090aa4264b73dc6868e824aa6` | 309 | 5 PASS / 0 FAIL | completed / failed / complete / ineligible |
| v4 candidate | `20261002-143618594-d6de758387724656af10fe557f3364cd` | 309 | 5 PASS / 0 FAIL | completed / failed / complete / ineligible |
| v4 negative | `20261002-143745364-1ce88b8b9c7d4c848608b401adf6f536` | 304 | 0 PASS / 1 FAIL | completed / failed / complete / ineligible |

All six native processes exit zero, collect fully with zero exclusions and qualify private cold Mesa disk writes **without waiver**. All are launched/collected through HTTP; no live preview, pause, input, diagnostics or bulk-transfer intervention. Private HDD clones are deleted only after extraction/finalization; cleanup receipts are preserved.

## Host checks and remaining gates

Full host discovery: **177 passing**. Generated catalog and diff checks pass; formatted new C++ compiles in pinned-NXDK Release (NXDK `73c9590`, pbkitplusplus `e91d509`). New MCPX source has no compiler warnings; the old `.edata` linker warning is retained. Review exposed the crossing-observation gap and omitted setup result; both corrected. The broader check exposed the strict package category omission; fixed with coverage.

Still unqualified: audible audio/DSP output, retail hardware, approved framebuffer references, valid affected/control fixed-work XISO performance, arbitrary backing destruction/resize, and repeatable game benefit. This fixture and xemu PR #275 remain draft; no merge.

## Archive

`EVIDENCE.tar.gz` preserves all non-state native payloads, exact guest outputs, original assessments, input/cache/extraction/cleanup receipts, earlier fixture images/catalogs, source, build/observer failures and reusable preparation/collection helpers. `INDEX.json` independently verifies every payload and hashes all collected state omitted from publication; state remains local. `SUMMARY.json` includes full raw leaf data, actual waits, identities and parity checks. Evidence belongs to the owning xemu PR; the test repository retains implementation only.
