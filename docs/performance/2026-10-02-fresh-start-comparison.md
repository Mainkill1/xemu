# Fresh-start performance — Windows and Steam Deck, 2026-10-02

## Scope and result identity

Each host ran **ABBA followed by BAAB** for each of three fresh-start games: eight attempts, four per executable, **48 scheduled attempts total**. All 48 attempts in the final complete schedules reached terminal completed/passed/complete/eligible outcomes under the runner's numeric checks. An earlier Windows Conker schedule stopped at attempt 6 on disk exhaustion; its five passing runs and failed sixth run were retained and excluded together. The entire eight-attempt schedule was repeated after archival, with the same procedure and execution runner. Startup qualifications and historical runs are excluded using all eight frozen archived IDs, rather than executable hashes alone. Their failures and limitations remain in the qualification audit.

**A:** upstream v0.8.136 (`fc24584ce88f0915ad7f04775bb7712c2e3f49ee`) plus matched buffered guest-flip logging at `6571e4dbbd287c7fedd4204246c265e57dc32207`. Original upstream emulation and locking are retained. This is an instrumented baseline, not the untouched official release binary.

**B:** fork main `ee5ce48b48784f999af374c1452003f8b2b1230f`. This compares the cumulative fork with upstream; it cannot attribute a change to one PR.

Both binaries report `0.8.136-0-g<source hash>`. Builds: [A CI](https://github.com/Mainkill1/xemu/actions/runs/36996851147), [B CI](https://github.com/Mainkill1/xemu/actions/runs/36839366349). All gameplay used runner `0186f52f18ab983954c473dd42f80c2ff660cbda`. Report analysis used the separately verified report-only `81b151e3298e437d903269e0c62ab530089c4081` after each host's acquisition completed. The pinned baseline and archived results were unchanged.

## Hardware and monitoring

| Rig | CPU | RAM | Selected GPU | OS / driver |
| --- | --- | --- | --- | --- |
| Windows | Ryzen 9 6900HX, 16 logical processors | 16,361,480,192 usable bytes (~16 GB) | RTX 3070 Ti Laptop GPU, 8 GiB | Windows 10 build 19045; NVIDIA 581.95 (Vulkan reports 581.380.0) |
| Steam Deck | AMD Custom APU 0405, 8 logical processors | 15,524,212,736 usable bytes (~16 GB) | RADV VANGOGH, shared memory | SteamOS; Mesa 25.3.0 git 59b552c765, LLVM 20.1.8 |

Process CPU is **core percent**: 100% is one logical core, so 300% is about three core equivalents. GPU is **whole-device utilization**, not xemu-specific usage. Windows used NVML only; Deck used AMD sysfs. Process-GPU samples were unavailable. Existing monitoring sampled every 500 ms, with sensors every 1000 ms; no second live sampler was added. Windows collector duty was about 10% of that sampling interval versus about 0.4% on Deck; exact per-attempt values are in the canonical metrics CSV. Monitoring is matched within each host, not overhead-free.

## Observed comparison

Cadence means guest `READ_3D` flip increments per host second, **not host-presented FPS or identical guest simulation progress**. CPU/frame values are server-calculated medians of four per-attempt estimates; raw frame samples are not pooled. GPU is the explicitly secondary median of four per-attempt means from the unchanged, hash-verified `metrics.csv` named segment.

| Rig / scene | Guest flips/s A → B | Cadence change | xemu CPU core % A → B | Device GPU % A → B | p99 ms A → B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Windows / PGR2 parked start | 14.66 → 30.00 | +104.67% | 316.7 → 321.2 | 37.7 → 43.5 | 83.18 → 34.00 |
| Windows / Conker bar menu | 29.18 → 30.00 | +2.80% | 252.6 → 248.2 | 52.2 → 56.1 | 50.06 → 33.75 |
| Windows / DOAXBV island menu | 60.00 → 60.00 | 0.00% | 280.6 → 247.4 | 39.5 → 45.0 | 17.49 → 17.04 |
| Steam Deck / PGR2 parked start | 7.38 → 7.15 | -3.18% | 299.8 → 403.5 | 30.2 → 57.8 | 242.72 → 335.92 |
| Steam Deck / Conker bar menu | 9.12 → 9.59 | +5.17% | 210.2 → 185.3 | 28.4 → 60.7 | 140.70 → 161.21 |
| Steam Deck / DOAXBV island menu | 42.74 → 47.95 | +12.19% | 189.6 → 196.3 | 42.5 → 59.2 | 37.13 → 26.51 |

The summary retains improvements and regressions. Different scene phases, driver cache state and unpinned power/thermal conditions limit causal interpretation. In particular, the Deck's PGR2 regression must not be hidden behind the Windows gain.

## Frame intervals and resource usage

Frame mean/p95/p99 are medians of four per-attempt estimates. **Worst max** is the server-reported highest per-attempt maximum across all four runs. CPU is the median of the per-attempt mean. All per-run counts, CPU mean/max/p95/p99, collector duty and frame mean/max/p95/p99 are retained in [per-run canonical metrics](../../evidence/fresh-start-performance-2026-10-02/per-run-canonical-metrics.csv).

| Rig / scene | Build | Flips/s | Mean ms | Worst max ms | p95 ms | p99 ms | CPU core % | GPU device % |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows / PGR2 parked start | A | 14.66 | 68.60 | 101.00 | 80.46 | 83.18 | 316.7 | 37.7 |
| Windows / PGR2 parked start | B | 30.00 | 33.33 | 34.66 | 33.61 | 34.00 | 321.2 | 43.5 |
| Windows / Conker bar menu | A | 29.18 | 34.41 | 53.34 | 49.68 | 50.06 | 252.6 | 52.2 |
| Windows / Conker bar menu | B | 30.00 | 33.33 | 34.72 | 33.57 | 33.75 | 248.2 | 56.1 |
| Windows / DOAXBV island menu | A | 60.00 | 16.67 | 24.69 | 16.87 | 17.49 | 280.6 | 39.5 |
| Windows / DOAXBV island menu | B | 60.00 | 16.67 | 17.67 | 16.91 | 17.04 | 247.4 | 45.0 |
| Steam Deck / PGR2 parked start | A | 7.38 | 137.31 | 333.80 | 189.23 | 242.72 | 299.8 | 30.2 |
| Steam Deck / PGR2 parked start | B | 7.15 | 133.63 | 618.87 | 270.16 | 335.92 | 403.5 | 57.8 |
| Steam Deck / Conker bar menu | A | 9.12 | 110.36 | 213.83 | 132.85 | 140.70 | 210.2 | 28.4 |
| Steam Deck / Conker bar menu | B | 9.59 | 105.05 | 208.60 | 137.04 | 161.21 | 185.3 | 60.7 |
| Steam Deck / DOAXBV island menu | A | 42.74 | 23.51 | 191.20 | 30.23 | 37.13 | 189.6 | 42.5 |
| Steam Deck / DOAXBV island menu | B | 47.95 | 20.68 | 100.01 | 24.85 | 26.51 | 196.3 | 59.2 |

## Every directional change over 1%

Positive improvement means better according to the existing metric contract, negative means worse. These percentages and verdicts come directly from the scoped server CSV; they are descriptive, not statistical significance. Neutral sample-count/duration changes are retained in the full comparison CSVs rather than interpreted as speedups.

| Rig / scene | Metric | A | B | Improvement % | Verdict |
| --- | --- | ---: | ---: | ---: | --- |
| Windows / PGR2 parked start | `guest/cadence_fps` | 14.658 | 30.000 | +104.67% | improved |
| Windows / PGR2 parked start | `guest/interval_max_ms` | 93.296 | 34.330 | +63.20% | improved |
| Windows / PGR2 parked start | `guest/interval_mean_ms` | 68.599 | 33.333 | +51.41% | improved |
| Windows / PGR2 parked start | `guest/interval_p50_ms` | 71.160 | 33.333 | +53.16% | improved |
| Windows / PGR2 parked start | `guest/interval_p95_ms` | 80.460 | 33.614 | +58.22% | improved |
| Windows / PGR2 parked start | `guest/interval_p99_ms` | 83.178 | 34.000 | +59.12% | improved |
| Windows / PGR2 parked start | `monitor/cpu_mean` | 316.719 | 321.182 | -1.41% | regressed |
| Windows / Conker bar menu | `guest/cadence_fps` | 29.183 | 30.000 | +2.80% | improved |
| Windows / Conker bar menu | `guest/interval_max_ms` | 50.888 | 34.157 | +32.88% | improved |
| Windows / Conker bar menu | `guest/interval_mean_ms` | 34.411 | 33.333 | +3.13% | improved |
| Windows / Conker bar menu | `guest/interval_p95_ms` | 49.682 | 33.575 | +32.42% | improved |
| Windows / Conker bar menu | `guest/interval_p99_ms` | 50.055 | 33.751 | +32.57% | improved |
| Windows / Conker bar menu | `monitor/collector_duty_mean` | 10.189 | 10.311 | -1.20% | regressed |
| Windows / Conker bar menu | `monitor/cpu_mean` | 252.617 | 248.231 | +1.74% | improved |
| Windows / Conker bar menu | `monitor/cpu_median` | 252.267 | 245.545 | +2.66% | improved |
| Windows / DOAXBV island menu | `guest/interval_max_ms` | 17.904 | 17.564 | +1.90% | improved |
| Windows / DOAXBV island menu | `guest/interval_p99_ms` | 17.488 | 17.037 | +2.58% | improved |
| Windows / DOAXBV island menu | `monitor/cpu_mean` | 280.620 | 247.449 | +11.82% | improved |
| Windows / DOAXBV island menu | `monitor/cpu_median` | 279.527 | 244.342 | +12.59% | improved |
| Steam Deck / PGR2 parked start | `guest/cadence_fps` | 7.381 | 7.145 | -3.18% | regressed |
| Steam Deck / PGR2 parked start | `guest/interval_max_ms` | 309.260 | 432.409 | -39.82% | regressed |
| Steam Deck / PGR2 parked start | `guest/interval_mean_ms` | 137.307 | 133.628 | +2.68% | improved |
| Steam Deck / PGR2 parked start | `guest/interval_p50_ms` | 132.832 | 116.487 | +12.31% | improved |
| Steam Deck / PGR2 parked start | `guest/interval_p95_ms` | 189.230 | 270.158 | -42.77% | regressed |
| Steam Deck / PGR2 parked start | `guest/interval_p99_ms` | 242.719 | 335.917 | -38.40% | regressed |
| Steam Deck / PGR2 parked start | `monitor/collector_duty_mean` | 0.398 | 0.451 | -13.15% | regressed |
| Steam Deck / PGR2 parked start | `monitor/cpu_mean` | 299.816 | 403.538 | -34.60% | regressed |
| Steam Deck / PGR2 parked start | `monitor/cpu_median` | 311.347 | 418.099 | -34.29% | regressed |
| Steam Deck / Conker bar menu | `guest/cadence_fps` | 9.119 | 9.590 | +5.17% | improved |
| Steam Deck / Conker bar menu | `guest/interval_max_ms` | 198.907 | 183.950 | +7.52% | improved |
| Steam Deck / Conker bar menu | `guest/interval_mean_ms` | 110.361 | 105.053 | +4.81% | improved |
| Steam Deck / Conker bar menu | `guest/interval_p50_ms` | 108.910 | 100.044 | +8.14% | improved |
| Steam Deck / Conker bar menu | `guest/interval_p95_ms` | 132.847 | 137.041 | -3.16% | regressed |
| Steam Deck / Conker bar menu | `guest/interval_p99_ms` | 140.703 | 161.209 | -14.57% | regressed |
| Steam Deck / Conker bar menu | `monitor/collector_duty_mean` | 0.413 | 0.361 | +12.49% | improved |
| Steam Deck / Conker bar menu | `monitor/cpu_mean` | 210.183 | 185.258 | +11.86% | improved |
| Steam Deck / Conker bar menu | `monitor/cpu_median` | 213.246 | 183.269 | +14.06% | improved |
| Steam Deck / DOAXBV island menu | `guest/cadence_fps` | 42.744 | 47.955 | +12.19% | improved |
| Steam Deck / DOAXBV island menu | `guest/interval_max_ms` | 184.789 | 57.022 | +69.14% | improved |
| Steam Deck / DOAXBV island menu | `guest/interval_mean_ms` | 23.509 | 20.676 | +12.05% | improved |
| Steam Deck / DOAXBV island menu | `guest/interval_p50_ms` | 22.798 | 21.183 | +7.08% | improved |
| Steam Deck / DOAXBV island menu | `guest/interval_p95_ms` | 30.234 | 24.849 | +17.81% | improved |
| Steam Deck / DOAXBV island menu | `guest/interval_p99_ms` | 37.132 | 26.514 | +28.59% | improved |
| Steam Deck / DOAXBV island menu | `monitor/collector_duty_mean` | 0.384 | 0.371 | +3.40% | improved |
| Steam Deck / DOAXBV island menu | `monitor/cpu_mean` | 189.620 | 196.282 | -3.51% | regressed |
| Steam Deck / DOAXBV island menu | `monitor/cpu_median` | 189.440 | 183.503 | +3.13% | improved |

## Procedures and scene limits

- **PGR2:** existing `pgr2-parked-v1`, unchanged 11 menu inputs, Right for 80 ms, other holds of 100 ms, no acceleration or braking. Begin measurement immediately after final input. All end images show the same car/course at 0 mph. Representative Windows starts show countdown 3; representative Deck starts show the Hong Kong introductory camera. This is not an identical cross-host starting image or guest-time benchmark.
- **Conker: Live & Reloaded:** original one-A proposal and Start did not skip the movie. New `conker-menu-fresh-v2`: full boot, wait 15 s + 15 s, A for 100 ms, wait 20 s, A for 100 ms, wait 15 s to finish the bar transition, then measure 30 s. Native A/B qualifications on both hosts show Xbox Live & Co at start/end. No input during measurement. The user clarified that the desired 15-second delay starts at the Rare/Conker intro, then authorized qualification of a working skip sequence. This fixed-delay revision detects neither Xbox boot completion nor the Rare intro; it is not an exact 15-guest-second gate. Menu animation phase is not fixed.
- **DOAXBV:** requested wait 90 s/A for 100 ms/wait 5 s/A for 100 ms, then measure 30 s. All end images show island options. Representative Windows starts already show options; Deck starts are title-to-menu transitions, with the menu appearing afterward. Its animated island camera is not locked to identical guest time.

CPU/GPU use the authored 30-second segment plus its screenshot overhead. Guest statistics use the existing bounded 25-second frame tail and five flip records. Those windows are distinct, and the report does not pretend otherwise. Procedures require at least 160 guest-frame intervals and 55 host CPU samples; actual counts are retained. The native controller on Deck and explicit legacy keyboard on Windows differ; neither has a qualified external frame observer. No frame multipliers, estimated FPS gates or hidden fallback were used.

## Fixed inputs, cache and settings

Vulkan, guest 1× scale, host 1280×720, VSync off, shader cache off, cold isolated application state and fresh private HDD/EEPROM copies. PGR2 uses 128 MiB guest RAM; Conker/DOAXBV 64 MiB. The fork uses DSP JIT and default Prewarm/Fastpath. Upstream uses its original DSP interpreter and does not recognize the fork's `audio.use_dsp_jit` key. Those implementation differences are part of this cumulative product comparison; DSP execution was not matched by transplanting fork code into upstream. This is the tested product configuration, not isolated optimization attribution. Both Linux builds use the same 38 pinned bundled libraries.

Both hosts use the same game ISO bytes, firmware and 1.44 MB portable PGR2-save HDD seed. PGR2 retains each host's existing EEPROM, whose hashes differ; new games use the same EEPROM seed on both hosts. A full snapshot is not used. Native firmware, games, EEPROM and save payloads are not included in this public evidence package.

Windows could not qualify a private Vulkan driver-cache namespace; the shared-cache limitation was declared in the frozen definitions before balanced acquisition. No cold-driver comparison is claimed. Deck's private Mesa namespace qualified using the current merged runner; no waiver was used. OS page cache, power limits and thermal starting state were not controlled. ABBA/BAAB balances order but does not prove those influences absent.

An earlier Deck DOAXBV qualification displayed a disc error; its root cause remains unproven. One Conker fork preparation was rejected below the 1 GiB free-space floor before launch; the unchanged procedure later qualified after verified archival of inactive staging. Failed movie routes and those preparation/guest failures are retained separately and are not silently pooled into the eight scheduled repetitions. The initial Windows Conker schedule failed at attempt 6 because the system disk filled while recording metrics; attempts 7/8 were never started. The entire schedule was repeated after inactive staging was hash-verified and archived to D:. The final reports include only the complete replacement schedule; the failed schedule remains in the audit and retained raw evidence. The storage-headroom gap is tracked in [runner issue 92](https://github.com/Mainkill1/Xemu-Test-Runner/issues/92).

## Evidence and reproducibility

[Fixed inputs and settings](../../evidence/fresh-start-performance-2026-10-02/fixed-inputs.json) record firmware, ISO, HDD and EEPROM hashes, sanitized settings templates and the 38 shared Linux-library identities. No input payload is published.

[Campaign manifest](../../evidence/fresh-start-performance-2026-10-02/campaign.json) pins all 48 actual run IDs, application hashes, saved-test revisions, procedure hashes and report fingerprints. Each host/game folder contains its scoped canonical JSON, full CSV, Markdown, and eight original performance packets, monitoring CSVs, raw guest timing logs and start/end screenshots. [GPU secondary description](../../evidence/fresh-start-performance-2026-10-02/gpu-secondary.json) records sample counts, missing readings and source hashes. Its colocated script reproduces only that secondary GPU description.

[File manifest](../../evidence/fresh-start-performance-2026-10-02/files.json) lists retained bytes and SHA-256 digests. [Qualification audit](../../evidence/fresh-start-performance-2026-10-02/qualification-audit.json) lists excluded startup attempts and known route failures. Hostnames were removed from exported inventories; no game or firmware bytes were published.

Shared procedure sources are in [runner PR90](https://github.com/Mainkill1/Xemu-Test-Runner/pull/90). Explicit generic comparison scope is in [runner PR91](https://github.com/Mainkill1/Xemu-Test-Runner/pull/91); XISO retains its own existing campaign report and child selection. This campaign adds no full-XISO timing or oracle claim and does not supersede the older XISO audit.
