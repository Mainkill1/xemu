# PR #276: equal APU voice-store investigation, 2026-10-01

**Decision: HOLD.** This is a product-code candidate with measured directional PGR2 improvement, not a qualified audio-correctness fix. Do not merge from this evidence.

## Identity and method

- Parent A: `ee5ce48b48784f999af374c1452003f8b2b1230f`. Candidate B executable source: `93b422636021ad613ac0ad1f47103cbab6e1e470`; the source/test tree at `8c9cc54d466b9f026ecee35a502c0fad32dbd5fb` has the same full tree hash, `73f854db60c7fb60abe4c7f957378ba9c0b9aa65`. This evidence commit adds documentation only.
- Windows executable SHA-256: A `a89521cb6ff76916e248b002f253df0c2d5aafb3867d13384d96a498c36622e2`, B `ba5afaca0408f77bf7bd43a96d1570cf4c8035174b075578bcecc695b2748940`. Deck executable SHA-256: A `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`, B `1fa82da6e460463d16e3664f6b2f5c139456614e90241bef4887bc06e30edffe`.
- Vulkan PGR2 with identical procedure per host for A and B; Windows procedure `issue247-win-pgr2-equal-store-common@783958562f437def56dad00b8a173bb3c02f35e8a3779917e5e3f45f460e2027`; Deck procedure `issue197-deck-pgr2-parked-window@ccccfd12ca5c8b64469f99d712329e7b6ec49e0000ef3175402b4dbd49d0920a`. Both retain the parked car, with no acceleration.
- Order per host: **A/B/B/A, then B/A/A/B**. Windows also ran one extra B/A/A/B block after the candidate B4 CPU spike, reported separately. Within each host the same race, car, and parking position appear in the start/end screenshots; the Windows window begins at the countdown while the Deck procedure includes a 35-second settle and begins after it. No cross-host pooling.
- Monitoring segment `stationary-start`; CPU is mean xemu process core-percent over the sampled segment. Frame average, p95, p99 and max are runner guest-frame intervals in milliseconds, using the final 25 seconds. Guest cadence is guest flips/second. Group summaries below are arithmetic means of the **per-run** statistics, not percentiles from a pooled interval stream. Lower CPU and interval values are better; higher cadence is better.
- Windows runner assessment: all eight primary attempts completed with correctness passed, evidence complete, and comparison eligible. Deck: all eight completed with correctness passed and evidence complete, but comparison ineligible because driver cache state was uncontrolled. The screenshots are private tester artifacts; run IDs below identify the retained records and `screenshot-sha256.txt` pins their returned bytes. Machine-readable PGR2 and XISO results are in `pgr2-runs.json` and `xiso-control-runs.json`. The operator-installed tester build identity was not independently pinned in this evidence.

## PGR2 Windows primary campaign

| Order | Variant | Run ID | CPU core % | FPS | Frame avg ms | p95 ms | p99 ms | Max ms | Runner comparison |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| A1 | A | `20261001-110513209-21e515a500564f7cb52f6f02a7e12799` | 317.65 | 30.00 | 33.33 | 33.56 | 34.05 | 34.37 | eligible |
| B1 | B | `20261001-110652588-1bc335f462cb4556b851bdec82b7e463` | 306.30 | 30.00 | 33.33 | 33.62 | 34.19 | 34.49 | eligible |
| B2 | B | `20261001-110831764-23b0105dc84346eeaef6467cb51847d2` | 309.04 | 30.00 | 33.33 | 33.58 | 33.89 | 34.31 | eligible |
| A2 | A | `20261001-111010672-a8b5e0383d7348e2aaf5929985018d00` | 319.24 | 30.00 | 33.33 | 33.59 | 33.99 | 34.19 | eligible |
| B3 | B | `20261001-111150159-b465647a2c8449b2adfeedd619ba1ff5` | 305.92 | 30.00 | 33.33 | 33.55 | 33.87 | 34.38 | eligible |
| A3 | A | `20261001-111330084-9e638ac360aa4c8faaa9460be280c4ff` | 329.50 | 30.00 | 33.33 | 33.58 | 33.90 | 34.28 | eligible |
| A4 | A | `20261001-111509402-f9bd09b9a3a64b89ab198ca652c6c0a6` | 314.66 | 30.00 | 33.33 | 33.61 | 33.77 | 34.21 | eligible |
| B4 | B | `20261001-111648751-69a0c58539af41ebaae8a1a0e384df00` | 334.41 | 30.00 | 33.33 | 33.64 | 33.92 | 34.47 | eligible |

| Metric, mean of four runs | Parent A | Candidate B | Difference B − A | Improvement % |
| --- | ---: | ---: | ---: | ---: |
| CPU core % | 320.263 % | 313.918 % | -6.345 % | +1.98% |
| Guest cadence | 30.000 fps | 30.000 fps | -0.000 fps | -0.00% |
| Frame average | 33.333 ms | 33.334 ms | +0.000 ms | -0.00% |
| Frame p95 | 33.583 ms | 33.596 ms | +0.013 ms | -0.04% |
| Frame p99 | 33.928 ms | 33.968 ms | +0.040 ms | -0.12% |
| Frame maximum | 34.263 ms | 34.415 ms | +0.153 ms | -0.45% |

Windows remains at its 30 fps game cap. Candidate B4 has the highest observed CPU mean, **334.41 core %**; it is included in the four-run result above. Its screenshot shows the same race, parked car and 000 MPH. The extra block below checks whether the spike repeats.

## PGR2 Steam Deck primary campaign

| Order | Variant | Run ID | CPU core % | FPS | Frame avg ms | p95 ms | p99 ms | Max ms | Runner comparison |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| A1 | A | `20261001-110501390-697d2744f33542e7ba66138cf1d662ba` | 316.02 | 18.45 | 52.41 | 69.21 | 103.29 | 146.68 | ineligible |
| B1 | B | `20261001-110758431-acea2e4308f8434493d36be947d4f602` | 309.34 | 19.24 | 50.83 | 68.26 | 103.25 | 187.79 | ineligible |
| B2 | B | `20261001-111054469-d97c0e8ba44b429b885395b35f21d484` | 307.96 | 18.86 | 51.40 | 68.69 | 95.52 | 156.79 | ineligible |
| A2 | A | `20261001-111351308-9ac1bc5d9e6745d3a012de6bd3a74b12` | 314.42 | 18.67 | 51.55 | 66.13 | 105.90 | 163.06 | ineligible |
| B3 | B | `20261001-111647887-10c6c90393e345fa935f8e7543ee46a7` | 308.57 | 19.51 | 50.15 | 66.48 | 89.15 | 155.19 | ineligible |
| A3 | A | `20261001-111945203-403059ad9644461b84179f308daa66a5` | 314.89 | 18.39 | 52.55 | 69.28 | 92.05 | 173.99 | ineligible |
| A4 | A | `20261001-112241481-6a2ef14510ae4cb3b9652063b8681f11` | 316.75 | 18.43 | 52.23 | 72.10 | 102.27 | 192.04 | ineligible |
| B4 | B | `20261001-112539010-853a48eb70f8495bb96a821d7f9d5126` | 308.99 | 19.02 | 50.66 | 70.70 | 85.21 | 202.38 | ineligible |

| Metric, mean of four runs | Parent A | Candidate B | Difference B − A | Improvement % |
| --- | ---: | ---: | ---: | ---: |
| CPU core % | 315.522 % | 308.715 % | -6.807 % | +2.16% |
| Guest cadence | 18.485 fps | 19.154 fps | +0.669 fps | +3.62% |
| Frame average | 52.185 ms | 50.760 ms | -1.425 ms | +2.73% |
| Frame p95 | 69.182 ms | 68.532 ms | -0.650 ms | +0.94% |
| Frame p99 | 100.877 ms | 93.284 ms | -7.593 ms | +7.53% |
| Frame maximum | 168.941 ms | 175.537 ms | +6.596 ms | -3.90% |

All four B runs have lower CPU mean and higher guest cadence than all four A runs, but the runner correctly marks every Deck comparison ineligible because the Mesa driver cache was uncontrolled. The average of the per-run maximum frame intervals is worse on B; this remains visible in the table.

## Windows extra B/A/A/B block

| Order | Variant | Run ID | CPU core % | FPS | Frame avg ms | p95 ms | p99 ms | Max ms | Runner comparison |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| B5 | B | `20261001-112106362-b4522f4c06934bb7a86f069a5226685f` | 306.42 | 30.00 | 33.33 | 33.53 | 33.77 | 34.01 | eligible |
| A5 | A | `20261001-112245881-df15d2cfcc5640269259f2b712c1c67b` | 316.44 | 30.00 | 33.33 | 33.56 | 33.83 | 34.27 | eligible |
| A6 | A | `20261001-112425738-915cb37d082a4b2f971daffc6842b7d2` | 323.35 | 30.01 | 33.33 | 33.61 | 34.16 | 34.38 | eligible |
| B6 | B | `20261001-112605422-c06df96b846d40498eb35968f0d61300` | 315.76 | 30.00 | 33.33 | 33.66 | 33.97 | 34.62 | eligible |

Extra-block CPU mean: A **319.897** versus B **311.088** core %, a **2.75%** observed reduction. B4 did not repeat; this block does not erase it.

## XISO control: `cpu_floating_point.sse_scalar` on Windows

The installed 156-leaf catalog has no APU/audio case. This CPU case is an **unaffected control**, not proof for APU voice writes. The selected leaf and injected plan matched in all eight attempts; the guest wrote `PASS` and framebuffer FNV-1a `6962077c244da325` every time. All eight runner assessments nevertheless report `Correctness: failed` and `Comparison: ineligible`: the leaf lacks a pinned oracle and the storage/driver-cache policy is uncontrolled. No result in this section is a qualified performance comparison.

| Block/order | Variant | Run ID | Guest outcome | Per-iteration avg ms | p95 ms | Max ms | Measured-loop total s | Runner correctness |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| ABBA-A1 | A | `20261001-112826683-ed2134f0ebb84f688372ed7d8949696e` | PASS | 530.318 | 554.789 | 554.789 | 5.303 | failed |
| ABBA-B1 | B | `20261001-112847207-291a713fb3e841dfaaaecae76ffc0b98` | PASS | 535.911 | 559.520 | 559.520 | 5.359 | failed |
| ABBA-B2 | B | `20261001-112907680-ae77417a1e164ccc8bf28a5ac860c7fc` | PASS | 563.301 | 585.892 | 585.892 | 5.633 | failed |
| ABBA-A2 | A | `20261001-112928442-63c7dc09461349fa8117637f49964e6f` | PASS | 530.712 | 543.160 | 543.160 | 5.307 | failed |
| BAAB-B1 | B | `20261001-113014688-b52e8ce4ca494bce8e4cc57af2ccc943` | PASS | 535.430 | 549.083 | 549.083 | 5.354 | failed |
| BAAB-A1 | A | `20261001-113035147-acba6ff3c8044e039bd4ed047f93a4af` | PASS | 528.555 | 539.079 | 539.079 | 5.286 | failed |
| BAAB-A2 | A | `20261001-113056019-66809e8a11b04b55914774c19a3e34b2` | PASS | 533.102 | 558.328 | 558.328 | 5.331 | failed |
| BAAB-B2 | B | `20261001-113116473-4dcfa9c05e61427e8b8595055d46adbd` | PASS | 552.810 | 559.206 | 559.206 | 5.528 | failed |

Unqualified arithmetic mean of the four guest-reported per-iteration averages: A **530.672 ms**, B **546.863 ms**, B slower by **16.191 ms (3.05%)**. This adverse observation is not hidden or attributed to the APU change; the test does not exercise the affected path and the runner rejected comparison. `total_us` is ten measured iterations per attempt, **not** whole-test wall time. The installed older ISO emitted no `guest/test-wall-times.jsonl`, so per-test and total wall-clock times are unavailable.

## Applicability and remaining gates

- The PGR2 data supports a limited, workload-specific CPU-cost reduction on Windows. It does not establish a frame-time improvement there; p95, p99, and maxima drifted slightly worse while cadence stayed capped. Deck trends favor B for CPU, cadence, mean and p99, but the cache qualification is missing and maximum intervals worsened.
- APU correctness remains unproven. `stl_le_phys` normally marks/invalidate dirty RAM even when bytes are equal. The guest CPU can write voice RAM between APU read and conditional skip, changing race outcomes. Review or redesign these semantics, then compare PCM output, dynamic overlays, reset/save-load, and affected audio workloads before recommending merge.
- Provide a pinned XISO oracle and APU/audio leaf, plus a guest-time file if wall-clock timing is required. Re-run affected OpenGL and Vulkan cases. Rebuild/requalify Deck with controlled cache state, and record the installed tester build identity. Do not pool the abandoned intermediate `7a507e8` callback-guard head or the earlier partial pilot with these results.
