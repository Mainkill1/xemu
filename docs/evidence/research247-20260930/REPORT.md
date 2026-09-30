# Issue 247 / PR 270: voice-write investigation

**Status: Investigating — no performance improvement demonstrated.**  
**Issue:** [#247](https://github.com/Mainkill1/xemu/issues/247) · **Draft code PR:** [#270](https://github.com/Mainkill1/xemu/pull/270)

## Summary

The existing APU helper reads a physical voice-state word, changes selected bits,
and writes the word back. This draft measures how often the resulting word is
unchanged. It **still performs every physical store**. An optimization that skips
stores has not been implemented or qualified.

Three Windows PGR2 launches found 88.19–88.23% unchanged writes. The admitted
Steam Deck launch found 88.62%. That identifies repeated work worth investigating;
it does not show that skipping it is safe or that xemu becomes faster.

The requested production baseline-versus-optimization comparison is outstanding.
The runner explicitly returns **`incomparable`** for previous main versus this
probe. Existing CPU readings are higher with the diagnostic executable, and its
same-binary collection-ON readings are higher than collection-OFF. Neither is an
accepted source-change improvement or regression measurement. Keep PR 270 draft.

## Investigation: why this patch exists

[Issue 247](https://github.com/Mainkill1/xemu/issues/247) proposes removing repeated
voice RAM stores. The measured production path is
[`voice_set_mask()` in vp.c](../../../hw/xbox/mcpx/apu/vp/vp.c#L121).
A physical store can also invalidate translated code and mark RAM dirty for
NV2A/VGA/migration. A guest write can interleave between the APU's read and store.
Read-time equality alone does not establish that omitting the store preserves
those effects or shared-register ordering.

The probe records register, envelope-phase, changed/unchanged and sampled store
cost buckets. Counters cover the **whole launch, navigation and race process**;
frame/CPU performance values cover the runner's named stationary-race segment.
Those scopes must not be conflated.

## Baseline, previous main and candidate

| Role | Exact source / executable | What it establishes |
| --- | --- | --- |
| Fixed cycle baseline | Product reference `bd1fecb93353272dda2a810991e28945de35b665`; retained Windows executable `3489fdcc593e942b92a612bf35a98f509ff0907e3370e1e5f45f2972d83fb16b` | Repository baseline from [baseline-selection.json](../../performance/baseline-selection.json); **not run under these test revisions** |
| Previous main | `2d289cb349bca95eae81b6a54b0f8d965365ff82`; CI Windows executable `70173532e9057a2dbebce15be1fea4b8482e4e780329375ca49cb7785439503d` | Two Windows reference observations; not a matched production comparison |
| Diagnostic candidate | Measured code `abd598cf596aff497d3b962bb309221f2d3092cc`; tree `c9759b7ab6067f2a9f51c6d3c88591293e5b4cde` | Measures equality and observer cost; preserves all stores |
| Candidate Windows executable | `cdc586252855f926002f7e4337a44aa7e692d6e5c5efc0b5f2b7c5d815089144` | One executable used with collection ON and OFF |
| Candidate Deck executable | `0f00290ec9b03d53b2f7237d884cb1a320c4fd850e76b5eea1250d34c59a155f` | Admitted Deck diagnostic; no matched Deck baseline |
| Behavioral optimization candidate | **None** | No equal-write shortcut or production Improvement result exists |

The previous-main binary comes from [CI run 36687928103](https://github.com/Mainkill1/xemu/actions/runs/36687928103).
The local probe uses different compiler/dependency/build settings from that CI
binary: native GCC 14/system SDL3 on Linux and MXE Clang with static dependencies
and an O2 development build on Windows. Later PR commits add documentation only.
The parent/probe comparison receipt is retained in
[runner-parent-versus-probe-comparison.json](runner-parent-versus-probe-comparison.json):
its status is `incomparable`, with no Improvement percentages. These build/test
mismatches must be resolved before a production comparison.

## Processing flow and code changes

| Existing main | This draft |
| --- | --- |
| Physical read → masked calculation → unconditional physical store | Same read/calculation → optional atomic counters/timing → **same unconditional physical store** |

`xemu_apu_voice_write_trace` defaults off. An enabled build collects only when
`XEMU_APU_VOICE_WRITE_TRACE` names an output file. Approximately 1/1024 calls use
a mixed sampling sequence; normal exit quiesces the APU before aggregation.
EF phases are 0–7, EA phases 8–15, and phase 16 is outside envelope stepping.
Debug active-entry exposure includes processed paused entries; it is not a
frame-end census of active guest voices.

Study attribution: the issue hypothesis references
`izzy2lost/xemu@e8e92c077a50ab7ec7075a88e0f0014be1071699`; no foreign implementation
source was opened or copied. [PR 189](https://github.com/Mainkill1/xemu/pull/189)
also touches vp.c for resampler selection; this draft changes no resampler policy.

## Profiling: repeated writes, not saved CPU

| Named diagnostic run | Store attempts | Unchanged writes | Unchanged | APU frames |
| --- | ---: | ---: | ---: | ---: |
| Windows probe ON pilot (`w1`) | 29,768,907 | 26,254,382 | 88.19% | 117,764 |
| Windows probe ON repeat 1 (`w2`) | 29,771,497 | 26,266,614 | 88.23% | 117,972 |
| Windows probe ON repeat 2 (`w3`) | 29,756,555 | 26,251,808 | 88.22% | 118,148 |
| Deck probe ON pilot, loading scene rejected (`d4`) | 30,677,098 | 27,447,860 | 89.47% | 175,173 |
| Deck probe ON admitted stationary race (`d5`) | 45,014,071 | 39,892,051 | 88.62% | 205,165 |

In the Windows pilot, envelope-count register `0x34` had 10,547,745 attempts,
99.6657% unchanged. Other measured equality ratios were `0x54` state 99.9398%,
`0x58` offset 64.6247%, and `0x5c` next 99.9123%. Off-phase envelope-count
updates dominate the repeated work. Original register/phase rows are retained
in each run's `voice-writes.csv`.

Sampled changed/unchanged store brackets averaged 162.76/172.23 ns in the pilot
and 164.90/176.91 ns in Windows ON repeat 1. Brackets include clock-read cost and
scheduling effects, exclude counter updates, and have coarse clock quantization.
They cannot be extrapolated into saved production CPU time.

## Performance results

Positive **Improvement** would mean lower CPU/intervals or higher useful guest
progress. A missing qualified comparison is shown explicitly below.

| Required comparison | Baseline | Candidate | Improvement | Result |
| --- | --- | --- | --- | --- |
| Optimization vs previous main | Previous main `2d289cb` | No behavioral optimization | **Not measured** | Outstanding |
| Optimization vs fixed cycle baseline | Fixed retained baseline above | No behavioral optimization | **Not measured** | Outstanding |
| Previous-main observations vs diagnostic probe | CI main; different build/test context | Local probe | **Not comparable** | Runner rejected comparison |
| Probe collection OFF vs ON | Same diagnostic executable, OFF repeats 1/2 | Same executable, ON repeats 1/2 | **Not qualified** | Descriptive observer check only |

### Retained Windows PGR2 observations

CPU percentage is summed across cores: 100% means one core. Frame intervals are
`+bad`; cadence is `+good`. Values are the runner's stored analysis, not statistics
recomputed from CSV. The ON/OFF/OFF/ON order is ON repeat 1, OFF repeat 1,
OFF repeat 2, ON repeat 2.

| Named run | CPU mean, core % | Guest cadence, fps | Mean interval, ms | p95, ms | p99, ms | Maximum, ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Previous-main repeat 1 (`main-aa1`) | 309.87 | 30.00 | 33.33 | 33.64 | 34.00 | 34.33 |
| Previous-main repeat 2 (`main-aa2`) | 311.10 | 30.00 | 33.33 | 33.65 | 34.14 | 34.64 |
| Probe ON pilot (`w1`) | 396.95 | 30.00 | 33.33 | 33.64 | 34.48 | 35.12 |
| Probe ON repeat 1 (`w2`) | 397.83 | 30.00 | 33.33 | 33.63 | 34.56 | 35.42 |
| Probe OFF repeat 1 (`off1`) | 380.02 | 30.00 | 33.33 | 33.52 | 34.32 | 35.47 |
| Probe OFF repeat 2 (`off2`) | 389.54 | 30.00 | 33.33 | 33.79 | 34.40 | 34.86 |
| Probe ON repeat 2 (`w3`) | 393.61 | 30.00 | 33.33 | 33.67 | 34.34 | 34.76 |

Collection ON uses more observed CPU than OFF in these repetitions; guest cadence
stays about 30 fps. Only two observer repetitions per setting, unmanaged cache
state, roughly 33–37% Windows collector duty, and race-start/countdown timing
variation prevent a causal overhead or production Improvement estimate.
Runtime OFF is still the diagnostic build; a default-OFF build removes the probe.

### Retained Steam Deck PGR2 observations

| Named run | CPU mean, core % | Cadence, fps | Mean interval, ms | p95, ms | p99, ms | Maximum, ms | Scene admission |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Probe ON pilot (`d4`) | 355.50 | 18.89 | 50.85 | 64.58 | 79.09 | 93.81 | **Rejected:** loading introduction at first checkpoint |
| Probe ON stationary race (`d5`) | 320.24 | 24.12 | 40.03 | 52.18 | 60.03 | 66.16 | Both checkpoints stationary at 000 MPH |

These are different saved seeds/procedures/race targets from Windows. No host
comparison or Deck baseline/candidate Improvement is supported. Deck driver-cache
qualification is unverified and its comparison is ineligible. No waiver was used.

## XISO, resources and correctness

| Gate | Result / limit |
| --- | --- |
| Production collector units | 4/4 pass, including concurrent writers and periodic sampling negative control |
| Collector sanitizers | ASan/UBSan 4/4 pass |
| Emulator builds | Native diagnostic, native default-OFF and Windows diagnostic complete |
| Draft CI | [Run 36705664546](https://github.com/Mainkill1/xemu/actions/runs/36705664546) passes platform builds/unit job |
| Adjacent local resampler test | Existing GCC 14 compile failure: missing `<math.h>`; full local suite is not claimed green |
| Full XISO for a behavioral candidate | **Not performed** |
| PCM/audio and guest-state equivalence | **Not performed** |
| RAM translation, dirty effects, reset/save/load, guest writers | **Not proved** |
| GPU/memory/cache-growth comparison | **No qualified aggregate comparison collected** |

Existing graphics/header/third-party warnings are retained; there is no entirely
warning-free build claim. Review repaired aggregate loss on normal quit and
sampling aliasing. Exact trace count/byte invariants and runner analysis source
hashes are checked by `audit.py`.

## Run names and original identifiers

`w` meant **Windows**, `d` meant **Steam Deck**, `off` meant **Windows collection
disabled**, and `main-aa` meant **two observations of the same previous-main
binary**. Numbers identify attempts/repetitions, not versions or scores. The
human-readable names below are authoritative; old directory names remain only
to preserve the original evidence identities.

| Named run | Old label | Outcome | Canonical result |
| --- | --- | --- | --- |
| Windows previous-main repeat 1 | `main-aa1` | Complete/pass; comparison ineligible | [result](runs/main-aa1/20260930-094317816-5901d683a67541b4b186d53405cdb128/result.json) |
| Windows previous-main repeat 2 | `main-aa2` | Complete/pass; comparison ineligible | [result](runs/main-aa2/20260930-095602990-cbfd87824f674b5da2ac62faa5589f61/result.json) |
| Windows probe ON pilot | `w1` | Complete/pass | [result](runs/w1/20260930-102410994-f93b071605734724a8a39ad501582fa8/result.json) |
| Windows probe ON repeat 1 | `w2` | Complete/pass | [result](runs/w2/20260930-102727381-805862b9c2ec4c46a7449d919da8604f/result.json) |
| Windows probe OFF repeat 1 | `off1` | Complete/pass | [result](runs/off1/20260930-103601681-b66b598609c64a7585a681d44682867c/result.json) |
| Windows probe OFF repeat 2 | `off2` | Complete/pass; comparison ineligible | [result](runs/off2/20260930-103855885-ce2ede4057de488ca0c4edda5a4c8bd9/result.json) |
| Windows probe ON repeat 2 | `w3` | Complete/pass | [result](runs/w3/20260930-104035873-7afda96492dc450bbbcce5ff731ae679/result.json) |
| Deck setup attempt 1: missing private paths | `d1` | Start failed; incomplete | [result](runs/d1/20260930-102509286-47d976bf532941ea8fc870f6700af86a/result.json) |
| Deck setup attempt 2: incompatible slirp | `d2` | Control error; exit 1; incomplete | [result](runs/d2/20260930-102921311-fc980115e64941698750b19ada203164/result.json) |
| Deck setup attempt 3: invalid controller button | `d3` | Plan failed; exit 137; incomplete trace | [result](runs/d3/20260930-103141398-f0af4140332c485a85ee5f7414ea6977/result.json) |
| Deck probe ON pilot: loading scene | `d4` | Complete/pass; **scene rejected** | [result](runs/d4/20260930-103401396-f936c1a9fc934be6ac28e1e4e0bfa937/result.json) |
| Deck probe ON admitted stationary race | `d5` | Complete/pass; comparison ineligible | [result](runs/d5/20260930-103846385-41140180f8fa4b7fa391d9ba8c6260a1/result.json) |

Older Windows runner eligibility labels do not qualify these diagnostic results
for production acceptance. Original failures and the rejected scene remain.

## Tradeoffs, decision and remaining validation

The measured equality rate warrants further investigation. It does not justify
skipping writes: ordinary RAM translation, observable dirty/invalidation effects
and guest-writer ordering are unresolved. Instrumentation perturbs timing.

Keep the existing stores and PR 270 draft. A behavioral candidate must first
prove those semantics and PCM/state/reset/load correctness, then run matched,
uninstrumented A/A, ABBA and BAAB against both previous main and the fixed cycle
baseline. Report average, p95/p99, actual maximum, useful guest progress, CPU,
resources and unfavorable repetitions. That acceptance work is still pending.

## Evidence and migration

All emulator evidence for this investigation now belongs in **Mainkill1/xemu**,
alongside draft PR 270. [summary.json](summary.json) carries named run roles;
[run-labels.json](run-labels.json) maps every original label to its meaning and
canonical result. Raw guest/runner logs and artifacts remain byte-exact.

`ORIGINAL-REPORT.md`, `ORIGINAL-audit.py`, `ORIGINAL-summary.json` and
`SOURCE-SHA256SUMS` preserve the prior published snapshot. `audit.py` verifies its
original file hashes before checking numeric trace invariants and exact runner
analysis sources, then writes the current summary/manifest. Run:

```sh
python3 audit.py
sha256sum -c SHA256SUMS
```

No executable, game disc, firmware, writable HDD, credentials or driver-cache
binary is published. Operator setup and build details remain in the original
report; normal testing used the maintained HTTP clients. Agent/model: Codex
(GPT-6), with read-only review by a Codex subagent using the inherited model.
