# Four voice workers: clean upstream native study

## Scope and result

Completed 48 selected native observations: three titles on Windows and Steam Deck, eight cells per cohort in ABBA then BAAB order. A retains upstream Auto; B caps Auto at four. Both use default sinc and the same instrumentation. This is a worker-policy comparison, not a comparison of the entire fork against upstream. All 56 attempts are retained: 48 selected, five from interrupted Deck BAAB blocks, and three pre-game ABI failures.

**Conclusion:** Recorded CPU is lower in all six measured cohorts and in both run orders. Deck Conker has an observed 9.31% higher p99 (158.12 → 172.83 ms); its ABBA FPS result is 6.24% lower, while BAAB is 0.30% higher. Neither favorable nor adverse tail changes are proven worker-count effects: identical wall-clock inputs can reach different guest progress and scene work. The component fixture separately establishes a CPU-cost reduction for fixed audio work. PR #200 remains HOLD for attribution and outstanding qualification.

The product patch changes only `voice_work_init()` in `hw/xbox/mcpx/apu/vp/vp.c`: Auto becomes `max(1, min(logical_cpus, 4))`. Explicit choices keep the original clamp. The emulator patch contains four added lines and one removed line. Configs remain Auto (`0`) in both arms.

## Builds and hardware

- Upstream: `478b4f496102379c7eaa7f3ec10e714a703c4300`.
- Reference A: `07a6df72ee485d92cdbc7be35af462ee2bfdca81`.
- Candidate B: `b079767d5b0ba5396d7f5653333c27366a5498c2`.
- Matched READ_3D logging source on both sides; no fork renderer, timer, resampler, or other audio fixes.
- Linux: Ubuntu 22.04 GCC 11.4, release/LTO/x86-v3. Windows: pinned GCC 16.1 cross toolchain, release/LTO/x86-v3. Each host uses the same compiler/options for A and B.
- Steam Deck: AMD Custom APU 0405, 8 logical processors, 16 GB class RAM, AMD Vangogh graphics/amdgpu, SteamOS. Expected workers: 8 → 4.
- Windows 10: Ryzen 9 6900HX, 16 logical processors, 16 GB class RAM, RTX 3070 Ti Laptop GPU plus Radeon integrated graphics. Expected workers: 16 → 4.
- Vulkan on both rigs. Runner revision: `3aa5e059d52339bf4b14725260e124322c30beca`.
- Executable and configuration identities are retained in the manifest and per-run records. Version strings are assigned as `0.8.136-0-g<source>`.

## CPU and average guest FPS

Each entry is the median of four per-run means per arm. CPU is summed process utilization, with one logical core = 100%; values can exceed 100%. FPS is 1000 divided by each run’s canonical analyzed mean guest READ_3D interval, not independently measured SDL-presented FPS. Positive improvement percentages mean a favorable observed difference, without proving attribution. CPU and frame windows also differ; these values must not be divided into a claimed fixed-work CPU-per-frame cost.

| Host / game | CPU A → B | CPU reduction | Avg guest FPS A → B | FPS change | GPU device A → B | Eligible runs |
|---|---:|---:|---:|---:|---:|---:|
| win / morrowind | 250.49 → 244.66% | +2.33% | 24.98 → 25.62 | +2.57% | 38.45 → 38.84% | 0/8 |
| win / conker | 254.38 → 247.21% | +2.82% | 29.17 → 29.56 | +1.34% | 53.09 → 53.74% | 8/8 |
| win / pgr2 | 292.12 → 256.65% | +12.14% | 24.17 → 24.65 | +2.02% | 35.20 → 35.83% | 8/8 |
| deck / morrowind | 180.33 → 163.48% | +9.35% | 6.79 → 7.19 | +5.93% | 70.60 → 68.70% | 8/8 |
| deck / conker | 217.85 → 199.26% | +8.53% | 7.86 → 7.90 | +0.43% | 25.56 → 26.13% | 8/8 |
| deck / pgr2 | 293.12 → 268.97% | +8.24% | 8.32 → 8.39 | +0.89% | 38.17 → 34.26% | 8/8 |

## Frame times

Milliseconds; lower is better. Each percentile is calculated per run, then the four run values are reduced by median. Median max and worst observed max are shown separately. These numbers use selected complete blocks; excluded baseline traces contain ~5-second intervals, retained under failed attempts.

| Host / game | Mean A → B | p95 A → B | p99 A → B | Median max A → B | Worst max A → B | p99 improvement |
|---|---:|---:|---:|---:|---:|---:|
| win / morrowind | 40.03 → 39.03 | 50.15 → 50.09 | 50.41 → 50.23 | 66.74 → 66.69 | 67.82 → 66.77 | +0.35% |
| win / conker | 34.29 → 33.83 | 42.05 → 34.03 | 50.09 → 50.04 | 50.32 → 50.21 | 51.00 → 50.24 | +0.11% |
| win / pgr2 | 41.38 → 40.56 | 49.42 → 48.87 | 52.05 → 51.87 | 55.02 → 55.38 | 58.65 → 61.12 | +0.34% |
| deck / morrowind | 147.42 → 139.10 | 193.34 → 183.60 | 226.03 → 217.00 | 238.44 → 245.94 | 282.53 → 281.60 | +3.99% |
| deck / conker | 127.17 → 126.62 | 150.15 → 150.15 | 158.12 → 172.83 | 183.92 → 217.59 | 263.72 → 220.09 | -9.31% |
| deck / pgr2 | 120.22 → 119.16 | 175.19 → 169.53 | 210.24 → 208.08 | 246.32 → 228.17 | 262.49 → 247.96 | +1.03% |

## Run-order checks

| Host / game | CPU improvement ABBA / BAAB | FPS improvement ABBA / BAAB | p99 improvement ABBA / BAAB |
|---|---:|---:|---:|
| win / morrowind | +3.13% / +1.78% | +2.53% / +2.69% | +0.01% / +0.82% |
| win / conker | +3.00% / +2.82% | -0.30% / +3.28% | -0.07% / +0.27% |
| win / pgr2 | +12.11% / +11.38% | +3.57% / +0.95% | +1.84% / -0.58% |
| deck / morrowind | +8.80% / +10.48% | +2.09% / +10.55% | +11.22% / -4.08% |
| deck / conker | +7.61% / +8.98% | -6.24% / +0.30% | -13.31% / -11.81% |
| deck / pgr2 | +8.17% / +8.38% | -2.80% / +4.12% | +4.56% / -0.10% |

## What the recorded spikes establish

Offline inspection of the unchanged 48 traces exactly reproduces every canonical interval count, mean, p95, p99, and maximum. `spike-inspection.json` retains each run’s ten longest intervals, their READ_3D ordinals, relative times, and five-second summaries. No extra game runs, exclusions, thresholds, or benchmark edits were introduced.

| Host / game | Positive intervals per run | Intervals at/above per-run p99 | Location of those interval endpoints in analyzed window |
|---|---:|---:|---:|
| deck / morrowind | 160–182 | 2–2 | 4.27–25.00 s |
| deck / conker | 197–227 | 2–3 | 9.06–25.00 s |
| deck / pgr2 | 196–213 | 2–3 | 1.55–22.09 s |
| win / morrowind | 618–646 | 7–7 | 1.10–24.68 s |
| win / conker | 716–742 | 8–8 | 0.90–25.00 s |
| win / pgr2 | 600–628 | 6–7 | 0.08–18.66 s |

- Deck PGR2 intervals above 200 ms cluster early: A has 9/12 and B 10/15 in the first five seconds of their analyzed windows. Both arms can still have later long intervals. Different flyover/countdown progress is a plausible confound; this does not identify an audio or renderer cause.
- Deck Conker’s last recorded interval is the longest in 6/8 selected runs. Four of the five intervals above 200 ms end in the final five seconds. The saved plan includes an end screenshot before segment end and quit, making capture/lifecycle timing another candidate explanation. There is no trace linking a particular interval to that operation, so this is not a demonstrated screenshot defect.
- Deck p99 is estimated from only about two or three upper-tail observations per run. A change in which expensive work is reached can shift the percentile without a general reduction in stuttering; the converse applies to a higher p99.
- READ_3D ordinals count guest flip events from process start; they are not guest simulation time, identical object/pass identities, GPU completion, or SDL presentation generations. More ordinals in 25 host seconds do not prove matched scene work.
- Only endpoint images were captured. There is no shared clock anchor between the monotonic frame trace and UTC CPU/GPU samples, per-voice processing timeline, or GPU wait attribution. Exact spike causes and the same-scene p99 effect therefore remain unknown.
- A future targeted diagnosis would correlate one clock/scene marker with guest progress, voice-worker completion, and GPU wait/submit events on both builds. That would be a separately identified diagnostic run; the frozen benchmarks remain unchanged.


## Procedure and integrity checks

- Frozen procedure IDs/revisions are in `verification.json`. Within each host/title, all eight plans, arguments, timing settings, snapshot selections, controller transport, consumed dependencies, and configurations are identical.
- PGR2 remains the original parked procedure: no acceleration, all gameplay button durations at most 100 ms, measurement starts immediately after the last input. No pre-measurement wait was added.
- Morrowind uses the existing snapshot and Start/B procedure; Conker uses the existing qualified fresh-start skip procedure. No new gameplay inputs were inserted.
- Every launch/input-manifest executable and configuration identity matches its immutable arm manifest. All 96 start/end scene images were manually inspected; no within-host scene failure was found.
- All 48 startup logs name the intended Vulkan GPU: RTX 3070 Ti Laptop on Windows, RADV Vangogh on Deck. No software-renderer substitution occurred.
- All 48 runs completed the declared plan and correctness/evidence checks. Collector overruns are zero. These checks do not establish audio PCM equivalence or exhaustive rendering correctness.

## Limits and failed attempts

- Windows Morrowind is **diagnostic only**: all eight runs are comparison-ineligible because the original saved procedure does not qualify state/driver-cache isolation. Its numeric trend is retained and is not a qualified gain.
- Windows Conker/PGR2 use their original explicit uncontrolled-driver-cache waiver; they are runner-admitted, not proven driver-cache-controlled. Deck retains cold/private Mesa isolation. All policies and their differences are retained in `verification.json`; no waiver or baseline policy was changed.
- PGR2 has a known cross-host measurement-start phase difference (Deck flyover versus Windows countdown). End images show the parked car. Within-host A/B is checked; this is not an identical cross-platform game-time workload.
- Deck uses native OS gamepad input; Windows uses its saved legacy keyboard transport. Same-host A/B matches; cross-platform input equivalence is not established.
- Original frame-analysis windows are preserved. Morrowind uses the frozen last-25-second window ending at the last retained frame, rather than exactly the entire nominal 30-second CPU segment. Per-run window bounds/sample counts are retained. Five-second flip summaries are kept separately.
- Identical buffered READ_3D logging operates within the upstream lock scope. It adds diagnostic overhead, can lose trailing buffered rows on forced exit, and does not identify host presentations. No blanket frame multiplier is used.
- Three initial Deck baseline attempts failed before gameplay because Debian 13 builds required newer libslirp/OpenSSL than the frozen Ubuntu 22 test libraries. They remain in `manifest.json`. Both arms were rebuilt with the original ABI for an independent v2 campaign; no test library or procedure was changed.
- Original Deck Conker BAAB cell 6 completed gameplay but failed the unchanged frame-evidence threshold: 154 positive intervals versus 160 required, including a 4,998.913 ms flip gap and eight zero-CPU samples. The process exited normally, and its start/end images showed the menu. The cause of that stall is unresolved.
- Original Deck PGR2 BAAB cell 7 similarly completed gameplay but had 157/160 intervals, a 5,004.650 ms flip gap, and eight zero-CPU samples. Both failures used reference A; no operator pause was recorded. Available host journals did not identify the cause. Neither incomplete run is evidence of a qualified gain or a candidate regression.
- Each interrupted BAAB block was repeated once with identical apps/configs/procedure. All five original partial-block samples remain in `all-attempts.json`; three complete samples are also excluded with their blocks. The selected Conker ABBA and BAAB blocks are separated by PGR2, so temporal/thermal drift remains a limitation. Selection policy and exact run IDs are in `selection.json`.
- Upstream diagnostic CI failed to acquire hosted runners during the GitHub Actions incident. Native runs use completed local exact-source builds. Fork #200 exact-head CI is separately green (22/22).
- All 53 gameplay attempts exit normally but emit the same upstream GLib `g_source_destroy` ref-count assertion on teardown in both arms. It is retained as an unresolved common warning, not a candidate-only regression or a proven explanation of the two mid-run stalls.
- GPU numbers are whole-device utilization from existing `gpu_pct` samples over the original CPU monitoring segment. Per-process GPU samples are unavailable. Lower device utilization alone is not a performance improvement; other desktop workloads may contribute.
- This study does not cover OpenGL, sustained high-voice gameplay, active audio save/load, PCM/listening comparison, or new XISO controls. Fewer workers can reduce CPU while increasing audio-frame processing latency.

## Integration status

Fork PR #200 implements Auto up to four independently of #189 and preserves the existing restart-required Audio → Advanced worker selector and explicit 1–16 choices. The fork setting study is published separately; it must not be confused with these compiled upstream-only binaries. The minimal upstream product patch is small enough for a focused proposal, but these measurements do not establish universal uplift or remove the documented qualification gaps.

Evidence is isolated from `main`; only numeric/provenance summaries are published. Raw logs, configurations, screenshots, and game assets remain in private local storage.

Files: `manifest.json`, `verification.json`, `selection.json`, `summary.json`, `per-run.json`, `per-run.csv`, `all-attempts.json`, `spike-inspection.json`, `upstream-product.patch`.
