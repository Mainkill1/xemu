# Four-worker setting study — October 5, 2026

Same clean fork main `afdde9eb62accd07686be104ebf4a9381785aaab` executable, default sinc, Auto (Deck 8 / Windows 16) versus explicit four. This is a setting-effect study, not native execution of PR #200's new compiled head. 48 completed runs: original frozen PGR2, Morrowind, Conker procedures, ABBA then BAAB, eight per title and host. Inputs and measurement boundaries unchanged.

Median of four per-run values per side. CPU uses one core = 100%. Guest READ_3D cadence is not SDL presentation FPS. Frame intervals are milliseconds. Deck AMD APU 0405 (8 logical CPUs), RADV VANGOGH; Windows Ryzen 9 6900HX (16 logical), RTX 3070 Ti Laptop.

| Host / title | CPU Auto → four | CPU reduction | Guest flips/s | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| deck / morrowind | 222.93 → 205.17 | +7.97% | 13.02 → 13.24 | 74.24 → 73.29 | 99.99 → 91.74 | 109.65 → 100.09 | 124.93 → 124.97 |
| deck / conker | 188.92 → 174.57 | +7.60% | 8.40 → 8.55 | 119.06 → 117.45 | 151.21 → 150.13 | 170.03 → 174.82 | 191.69 → 188.86 |
| deck / pgr2 | 406.77 → 384.88 | +5.38% | 6.93 → 7.75 | 120.73 → 110.08 | 289.58 → 257.04 | 350.26 → 316.08 | 388.44 → 361.17 |
| win / morrowind | 276.20 → 230.49 | +16.55% | 31.97 → 33.30 | 31.23 → 29.94 | 33.79 → 33.69 | 50.03 → 50.01 | 50.44 → 50.35 |
| win / conker | 246.05 → 210.52 | +14.44% | 30.00 → 30.00 | 33.33 → 33.33 | 33.56 → 33.62 | 33.71 → 33.74 | 34.19 → 33.88 |
| win / pgr2 | 330.82 → 262.93 | +20.52% | 30.00 → 30.00 | 33.33 → 33.33 | 33.63 → 33.69 | 34.11 → 34.38 | 34.41 → 34.72 |

All 96 scene endpoint images were inspected privately. No within-host scene failures; PGR2 starts in the Deck flyover versus Windows countdown, so cross-host phase is not synchronized. All PGR2 ends parked at 0 mph on the same Hong Kong course. Windows Morrowind has 0/8 driver-cache-qualified runs; its numbers are descriptive. Other five cohorts have 8/8 runner-eligible runs. All individual runs, ranges and separate order effects remain in JSON/CSV. CPU improvement in these three scenes does not establish an all-workload improvement.

Deck Conker p99 is 2.82% worse (ABBA 3.75% worse, BAAB 1.01% better). Smaller adverse Windows tails and Deck Morrowind max remain visible. PR #200 stays draft while upstream-only validation and XISO timing controls are pending. No game bytes, captures, host paths or config files are published here. Evidence branch must not merge into main.
