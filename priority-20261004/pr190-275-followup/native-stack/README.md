# Prior #190 + #275 versus main — native ABBA/BAAB

A: main `b4d69b24`; B: stacked `ccf5c441`. Eight frozen runs per host/title,
physical ABBA then BAAB; medians of four canonical per-run measurements per side.
Guest READ_3D cadence is not SDL presentation FPS. Frame values are milliseconds;
CPU is process usage with one core equal to 100%. Max columns are medians of per-run
maxima; the JSON retains the complete ranges, including the worst observed maximum.

| Host / title | Cadence A → B | Mean frame A → B | p95 A → B | p99 A → B | Max A → B | CPU A → B | Eligible |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deck / morrowind | 13.016 → 12.715 | 74.750 → 76.298 | 99.994 → 100.001 | 102.644 → 114.899 | 133.990 → 136.826 | 221.314 → 220.489 | 8/8 |
| deck / conker | 8.224 → 8.392 | 121.885 → 119.385 | 158.983 → 161.974 | 182.381 → 183.381 | 201.775 → 208.284 | 187.023 → 186.762 | 8/8 |
| deck / pgr2 | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | 7/8 |
| win / morrowind | 31.783 → 31.515 | 31.468 → 31.636 | 41.804 → 34.048 | 50.027 → 50.037 | 50.403 → 50.397 | 280.761 → 274.533 | 0/8 |
| win / conker | 30.000 → 30.000 | 33.333 → 33.333 | 33.569 → 33.519 | 33.785 → 33.706 | 34.316 → 33.849 | 249.782 → 251.711 | 8/8 |
| win / pgr2 | 30.000 → 30.000 | 33.333 → 33.333 | 33.591 → 33.622 | 34.092 → 34.022 | 34.444 → 34.410 | 322.097 → 325.900 | 8/8 |

## Every change exceeding 1%

Positive improvement means better throughput or lower time/cost. Both order blocks and
raw ranges are retained; small differences with overlapping variation are not confident gains.

- deck morrowind guestFlipCadence: -2.31% overall; ABBA -3.18%, BAAB -1.37%; A range [12.764237555621552, 13.230149784308948], B range [12.404087973926607, 13.003118282964873].
- deck morrowind avgFrameMs: -2.07% overall; ABBA -2.99%, BAAB -1.74%; A range [73.61416764705878, 75.81171299093644], B range [74.70211904761906, 78.67823270440256].
- deck morrowind p99FrameMs: -11.94% overall; ABBA -11.94%, BAAB -7.43%; A range [100.19198, 111.67699999999981], B range [110.88194999999963, 116.7203].
- deck morrowind maxFrameMs: -2.12% overall; ABBA -0.15%, BAAB +2.51%; A range [116.629, 159.676], B range [124.826, 139.485].
- deck conker guestFlipCadence: +2.04% overall; ABBA +1.74%, BAAB +2.94%; A range [8.151006979754758, 8.324873328766884], B range [8.376044211857709, 8.534383951393812].
- deck conker avgFrameMs: +2.05% overall; ABBA +1.99%, BAAB +2.53%; A range [120.3874615384616, 123.23549261083748], B range [117.72982629107983, 119.87622857142851].
- deck conker p95FrameMs: -1.88% overall; ABBA +1.18%, BAAB -3.11%; A range [150.5726, 166.678], B range [150.113, 166.61055].
- deck conker maxFrameMs: -3.23% overall; ABBA -16.19%, BAAB +5.23%; A range [185.74, 216.673], B range [194.876, 235.774].
- win morrowind cpu: +2.22% overall; ABBA +2.59%, BAAB -0.09%; A range [273.0596229508197, 284.246], B range [270.2553606557378, 285.4774754098361].
- win morrowind p95FrameMs: +18.55% overall; ABBA +32.21%, BAAB -0.08%; A range [33.7662, 66.68], B range [33.9604, 49.71139999999998].
- win conker maxFrameMs: +1.36% overall; ABBA +2.34%, BAAB +0.63%; A range [33.902, 35.403], B range [33.827, 34.347].
- win pgr2 cpu: -1.18% overall; ABBA +0.75%, BAAB -3.54%; A range [318.5081904761904, 325.5065238095239], B range [318.652746031746, 335.7235714285714].

## Limits

- These measurements precede the `7dcdcb0d` initialization correction. They are not current-head recovery measurements; live campaigns are stopped.
- Final Deck PGR2 candidate cell8 has black start/end output and failed correctness, so the seven passing cells do not form a complete ABBA/BAAB comparison. Its actual metrics and failure remain in the per-run archive.

- Deck Morrowind cadence and p99 are adverse in both current order blocks. Earlier main-only runs show substantial background tail variation; they are separate noise diagnostics, not pooled into this comparison or used to erase the adverse result.
- Windows Morrowind has unmanaged/uncontrolled cache state and is comparison-ineligible. Windows Conker/PGR2 retain their existing explicit uncontrolled-driver-cache waiver; no new waiver was introduced.
- PGR2 uses unchanged original inputs with no acceleration. Deck measurement begins during the course flyover and Windows during countdown; this is not a synchronized cross-host parked-start comparison.
- Menu animation, guest-time progress and unpinned power/thermal state remain limits. Image review establishes the observed scene, not identical guest state or audible equivalence.
- The component VP improvements are separately measured on the build host and must not be substituted for native game FPS.
- This numerical archive contains no game resource bytes, firmware, screenshots, PCM or private perf traces.
