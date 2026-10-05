# Current #190 + #275 versus main — native ABBA/BAAB

A: main `b4d69b24`; B: stacked `7dcdcb0d`. Stopped after eight Morrowind and two Conker cells per host; PGR2 not started.
Morrowind completed physical ABBA then BAAB; its rows use medians of four per-run measurements per side. Conker has only two cells per host, so no aggregate is reported.
Guest READ_3D cadence is not SDL presentation FPS. Frame values are milliseconds;
CPU is process usage with one core equal to 100%. Max columns are medians of per-run
maxima; the JSON retains the complete ranges, including the worst observed maximum.

| Host / title | Cadence A → B | Mean frame A → B | p95 A → B | p99 A → B | Max A → B | CPU A → B | Eligible |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deck / morrowind | 13.010 → 12.676 | 74.712 → 76.737 | 99.998 → 100.013 | 100.145 → 116.647 | 125.174 → 135.318 | 221.962 → 219.785 | 8/8 |
| deck / conker | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | 2/8 |
| deck / pgr2 | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | 0/8 |
| win / morrowind | 31.935 → 32.203 | 31.280 → 31.015 | 33.766 → 33.739 | 50.038 → 50.054 | 50.406 → 50.384 | 282.570 → 282.836 | 0/8 |
| win / conker | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | 2/8 |
| win / pgr2 | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | Unavailable | 0/8 |

## Every change exceeding 1%

Positive improvement means better throughput or lower time/cost. Both order blocks and
raw ranges are retained; small differences with overlapping variation are not confident gains.

- deck morrowind guestFlipCadence: -2.57% overall; ABBA -2.57%, BAAB -2.28%; A range [12.892472141053641, 13.225740675237212], B range [12.303292157917413, 13.218272254762928].
- deck morrowind avgFrameMs: -2.71% overall; ABBA -2.89%, BAAB -1.09%; A range [74.53785416666658, 76.5084024390244], B range [73.94329203539824, 79.01847634069401].
- deck morrowind p99FrameMs: -16.48% overall; ABBA -10.45%, BAAB -8.36%; A range [100.11852, 111.05064000000041], B range [100.1663, 116.80448].
- deck morrowind maxFrameMs: -8.10% overall; ABBA -172.74%, BAAB -3.51%; A range [116.621, 144.747], B range [133.271, 549.435].

## Limits

- Each host/title retains both physical order blocks and the full run ranges. Prior builds and historical runs are not pooled into the final candidate comparison.
- Windows Morrowind has unmanaged/uncontrolled cache state and is comparison-ineligible. Windows Conker/PGR2 retain their existing explicit uncontrolled-driver-cache waiver; no new waiver was introduced.
- PGR2 uses unchanged original inputs with no acceleration. Deck measurement begins during the course flyover and Windows during countdown; this is not a synchronized cross-host parked-start comparison.
- Menu animation, guest-time progress and unpinned power/thermal state remain limits. Image review establishes the observed scene, not identical guest state or audible equivalence.
- The component VP improvements are separately measured on the build host and must not be substituted for native game FPS.
- This numerical archive contains no game resource bytes, firmware, screenshots, PCM or private perf traces.

## Decision

HOLD: Deck Morrowind guest cadence regresses 2.57%, above the owner's 2% limit, and p99 cost increases 16.48%; both orders are adverse. Broad acquisition stopped after 20 attempts. No PGR2 attempt was launched. Both rigs are idle. The owner reiterated that causal diagnosis must precede additional gameplay. The initialization repair alone did not restore performance; no merge or fixed-game-performance claim.
