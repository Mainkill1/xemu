# Upstream voice workers: C = 3 versus D = 5

## Scope

Recorded 48 runs using the same already-qualified instrumented upstream reference executable per host. C explicitly selects three workers; D selects five. Schedule: C/D/D/C followed by D/C/C/D per title/host. The previous A/B study was not repeated and its results are not pooled here.

Upstream `478b4f496102379c7eaa7f3ec10e714a703c4300`; shared instrumented source `07a6df72ee485d92cdbc7be35af462ee2bfdca81`. Default sinc/Vulkan. No executable/compiler difference within either host; only `audio.vp.num_workers` changes between conditions. Frozen game procedures, baseline selections, other settings and inputs are unchanged.

Deck: AMD Custom APU 0405, 8 logical processors, 16 GB class RAM, RADV Vangogh/SteamOS; Ubuntu 22 GCC 11.4 release/LTO/x86-v3 build. Windows 10: Ryzen 9 6900HX, 16 logical processors, 16 GB class RAM, RTX 3070 Ti Laptop GPU; GCC 16.1 release/LTO/x86-v3 build.

## CPU, guest FPS and GPU observations

Median of four per-run values/condition. CPU is summed process utilization, one core = 100%. FPS is inverse canonical mean READ_3D interval, not independently measured SDL presentations. GPU is whole-device utilization. Positive improvement means D is favorable versus C; percentages describe observations, not causal same-scene proof.

| Host / game | CPU C → D | CPU improvement | Avg guest FPS C → D | Device GPU C → D | Eligible |
|---|---:|---:|---:|---:|---:|
| deck / morrowind | Withheld: rejected scene/correctness | — | — | — | 8/8 automatic only |
| deck / conker | Withheld: rejected scene/correctness | — | — | — | 7/8 automatic only |
| deck / pgr2 | 244.34 → 279.58% | -14.43% | 6.99 → 8.43 | 47.79 → 31.87% | 8/8 |
| win / morrowind | 244.74 → 251.93% | -2.94% | 25.58 → 24.54 | 38.75 → 38.12% | 0/8 |
| win / conker | 248.45 → 253.62% | -2.08% | 29.62 → 29.38 | 54.06 → 52.33% | 8/8 |
| win / pgr2 | 248.41 → 259.92% | -4.63% | 24.03 → 24.11 | 36.33 → 36.37% | 8/8 |

## Frame times

Milliseconds, lower is better. Mean/percentiles/max are first calculated per run and then reduced by median. Worst max is shown separately.

| Host / game | Mean C → D | p95 C → D | p99 C → D | Median max C → D | Worst max C → D |
|---|---:|---:|---:|---:|---:|
| deck / pgr2 | 143.13 → 118.62 | 218.17 → 174.47 | 264.47 → 212.58 | 309.78 → 248.88 | 515.56 → 373.73 |
| win / morrowind | 39.09 → 40.75 | 50.12 → 50.13 | 50.24 → 50.24 | 51.13 → 66.53 | 65.63 → 66.64 |
| win / conker | 33.76 → 34.04 | 33.90 → 34.34 | 50.01 → 50.04 | 50.23 → 51.06 | 50.97 → 51.12 |
| win / pgr2 | 41.61 → 41.48 | 49.82 → 50.02 | 53.29 → 52.84 | 61.18 → 55.86 | 61.94 → 59.93 |

## Order sensitivity

| Host / game | CPU improvement CDDC / DCCD | FPS improvement CDDC / DCCD | p99 improvement CDDC / DCCD |
|---|---:|---:|---:|
| deck / pgr2 | -13.34% / -14.43% | +15.51% / +22.15% | +16.64% / +28.50% |
| win / morrowind | -2.91% / -3.64% | -4.09% / -3.38% | +0.01% / -0.07% |
| win / conker | -1.64% / -3.12% | -0.81% / -1.25% | -0.07% / -0.04% |
| win / pgr2 | -4.63% / -2.36% | +0.47% / -4.91% | +2.44% / -4.94% |

## Observed changes exceeding 1%

D relative to C; favorable is positive for FPS and negative for CPU/frame duration. These are observed differences with the same eligibility and attribution limits as above.

- deck / pgr2: cpu +14.43%; avgGuestFps +20.66%; avgFrameMs -17.12%; p95FrameMs -20.03%; p99FrameMs -19.62%; maxFrameMs -19.66%; wholeDeviceGpu -33.32% (no inherent gain direction).
- win / morrowind (descriptive; comparison-ineligible): cpu +2.94%; avgGuestFps -4.08%; avgFrameMs +4.25%; maxFrameMs +30.12%; wholeDeviceGpu -1.64% (no inherent gain direction).
- win / conker: cpu +2.08%; p95FrameMs +1.31%; maxFrameMs +1.65%; wholeDeviceGpu -3.21% (no inherent gain direction).
- win / pgr2: cpu +4.63%; maxFrameMs -8.71%.

## Integrity and limits

- Manifest records executable/configuration identities, hardware and procedure revisions. Both conditions use identical executable bytes; consumed configuration identities are verified per run. Other plans and input dependencies match within each host/title.
- PGR2 retains eleven short menu inputs, no acceleration, and measurement immediately after the final input. Start/end images are reviewed manually. These endpoints do not establish identical intermediate scene progress.
- Windows PGR2 cell 8 begins during a bright fade with countdown 3 visible; every Windows endpoint pair still identifies the expected course and parked finish. The fading/animation phase is retained as a limitation, not claimed pixel-identical work.
- Deck Morrowind cells 3 (five workers) and 6 (three workers) have menu overlays at both endpoints. The entire cohort is withheld, despite automatic admission; all raw measurements remain in per-run records. No favorable individual runs are selected into a replacement comparison.
- Offline reproduction of the maintained scene fingerprint explains the false admission: both rejected Morrowind menu images differ by only 2 bits, within the frozen limit of 8. Valid scene images differ by 0–1 bits. This identifies a weakness in that scene contract, not the cause of the incomplete input transition; no gate or procedure was changed.
- Deck Conker cell 8 (five workers) remained in the opening movie at the start and produced a black end image. Its correctness check failed; process exit was normal (0). The entire cohort is withheld. Neither input-transition cause nor worker-count causality is established.
- Existing controller receipts for the rejected Deck runs and successful controls record the requested Start/B or A submissions, neutral releases, no reported input failure and complete cleanup. Failed Morrowind native submission intervals are approximately 300 ms Start / 200 ms B; failed Conker intervals are approximately 101 ms per A. OS submission is not guest consumption acknowledgment; these receipts do not establish why the game remained in the wrong scene.
- Windows Morrowind retains its original unqualified state/cache policy. Windows Conker/PGR2 retain explicit uncontrolled-driver-cache waivers. Deck retains cold/private Mesa isolation. No policy was relaxed for eligibility.
- Native gamepad on Deck and saved keyboard transport on Windows remain unchanged. Cross-platform transport equivalence and PGR2 flyover/countdown phase alignment remain unresolved.
- Host power, thermal and frequency state were not actively pinned. Whole-device GPU percentages include work outside xemu and cannot identify a bottleneck or shader cost.
- Frame metrics retain the frozen last-25-second tail ending at the last retained frame; CPU/GPU use the original named monitoring segment. READ_3D is neither guest simulation time nor a GPU completion/presentation marker.
- Percentile changes may reflect different scene work or capture/lifecycle timing. Exact spike causes remain unresolved without a shared clock/progress and worker/GPU timeline. CPU and FPS cannot be combined into claimed fixed-work CPU/frame cost.
- OpenGL, deterministic PCM/listening, active reset/save-load and new XISO controls were not performed for this setting-only study. Any incomplete or failed observations remain in per-run data and cannot establish a gain.
- The older four-worker A/B results provide historical context only; this is not a contemporaneous controlled three/four/five experiment.

## Files

`manifest.json`, `verification.json`, `per-run.json`, `per-run.csv`, `summary.json`, `scene-fingerprint-audit.json`, `controller-input-audit.json`. Numeric evidence only; raw logs, screenshots and configurations remain private. This evidence branch is never merged into main.


## Spike inspection

`spike-inspection.json` reproduces the canonical raw interval summaries and records the ten longest intervals per run, their relative time and process-start READ_3D ordinal. Rejected Morrowind menu traces remain included as raw evidence, not valid performance comparisons. No additional native runs or threshold changes were introduced.
