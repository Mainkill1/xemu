# PGR2 original controls and runtime qualification

**Recommendation: HOLD.** These are original-only controls for issue #197 / PR #275. They do not measure candidate game performance. The existing component ABBA/BAAB result remains **48.04% lower mono read time / 54.35% lower stereo read time**; translating that saving into a game gain remains open.

## What was measured

Steam Deck `10.0.0.123`, maintained HTTP runner `f5b3e58`, original xemu `ee5ce48b48784f999af374c1452003f8b2b1230f`, executable SHA-256 `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`. Vulkan, full DSP/JIT, 128 MiB guest RAM, private HDD/EEPROM, private cold application/Mesa disk cache. Each run uses the same fixed input route and a stationary measurement lasting at least 300 seconds. CPU is process CPU, where 100% is one fully occupied logical core. Guest frame intervals and the final six flip-log samples are different measurements; neither is host presentation FPS.

| Original-only control | Excluded extra warmup | Mean guest frame interval | p95 / p99 interval | Mean process CPU | Tail guest cadence | Outcome |
|---|---:|---:|---:|---:|---:|---|
| v2 first | 0 s | 2,014.46 ms | 2,960.93 / 3,160.89 ms | 239.21% | 0.508 frames/s | Eligible; complete collection |
| v2 second | 0 s | 1,193.27 ms | 1,630.25 / 1,741.75 ms | 249.45% | 0.766 frames/s | Eligible; complete collection |
| v3 first | 60 s | 1,268.53 ms | 1,749.60 / 1,847.56 ms | 251.19% | 0.707 frames/s | Eligible; complete collection |

The v2 same-original mean intervals differ by **821.19 ms, or 40.76% of the first run**. That is measurement variation, not optimization improvement. Direct inspection of the earlier screenshots found a different countdown phase. The new immutable v3 test inserts one excluded 60-second warmup before the unchanged 300-second stationary segment. Its start/end screenshots show a red car parked at 0 MPH with opponents departing. One v3 run cannot establish that warmup improves repeatability; it must not be combined with v2 as an A/A pair because the workload changed.

All three runs exit zero and retain their original completed/passed/complete/eligible assessments. All **864 / 868 / 868** artifacts collect with zero exclusions. Each cache report records `DriverNamespaceVerified=true`, `DriverQualification=mesa_private_disk_writes_observed`, `ComparisonReady=true`, and no uncontrolled-driver-cache waiver. OS page cache and driver memory cache remain uncontrolled. The report verifies private disk-cache use, not a fully reset GPU or cold RAM.

## Why candidate game comparisons are still pending

Checking the corresponding original and current candidate AppImage libraries found **37 of 38 identical**. GLib differs in 493,876 byte positions despite equal 1,335,888-byte size; this is not just an ELF build-ID difference.

| GLib identity | Old original bundle | Current candidate bundle |
|---|---|---|
| SHA-256 | `96dbcd8324a278f45945c5745051dc0c669abed1238c323be70ea012b3a571c5` | `5bff7cc1178909087dd46f1ca3bbf9cdb28f95d7a7c68235835c10d4555f1e9e` |
| ELF build ID | `6b4f160dbc5397c2f502dc4f08a8cff259917926` | `597d5ce8aae9e583b78d96df7dbdd129994481c5` |

This does not explain variation within v2: both controls used the same old bundle. It prevents clean attribution in a comparison against the newer candidate bundle. No libraries were substituted and no measurement was repaired. The local helper was stopped before it could queue the second old-bundle v3 control; the first native job continued unchanged and completed normally. Its observer interruption is archived.

The exact original commit was pushed to the CI input branch `ci/issue197-main-reference-20261002`. [Reference CI run 37004023200](https://github.com/Mainkill1/xemu/actions/runs/37004023200) initially hit an ARM LLVM-package download error (`curl` exit 7), which canceled the x86 builds through matrix fail-fast. This is an infrastructure failure, not proof of a compilation defect. Original logs and job states remain archived. A new reference artifact must have matching compiler/build settings and all 38 corresponding libraries verified before new A/A controls and physical **ABBA then BAAB** game runs.

## Reusable test and evidence

Retained runner test: `issue197-deck-pgr2-parked-warm60-300-v3`, revision `3e72921bec480a32d141fe3b8c3894b6a64a864de037cee803d8fde52d7ef919`. All 39 executable/library slots are declared build inputs, so future replacements must supply the complete bundle. The helper, frozen workload, receipts, raw measurements, screenshots, original failures, and per-file collection ledgers are retained in [EVIDENCE.tar.gz](EVIDENCE.tar.gz). Do not rerun the historical helper against existing job IDs; use the retained immutable test with fresh explicit attempt IDs and a verified complete build.

[SUMMARY.json](SUMMARY.json) supplies exact values, run IDs, original outcomes, and the full 38-library comparison. [INDEX.json](INDEX.json) binds every archived payload and all locally retained guest/cache state to its SHA-256. The archive was reopened and every payload hash verified. Guest assets and collected shader/cache binaries are not duplicated in the repository; their complete collection receipts, ledgers, sizes, and hashes remain visible. No game gain, audio parity, or affected XISO coverage is established here.
