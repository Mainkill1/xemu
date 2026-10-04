<!-- SPDX-License-Identifier: GPL-2.0-or-later
-->

# PR #276 atomic equal-store revision: native PGR2 evidence

**Decision: HOLD.** The Windows CPU observation is directional; the race/memory-order contract, PCM, save/load, and affected audio workload are not qualified. This report measures executable source `d3ccb5420cdcaebf73ac9553d32f73c09c899d61`, not the earlier candidate documented in `REPORT.md`.

## Method and identity

- Parent A source: `ee5ce48b48784f999af374c1452003f8b2b1230f`. Atomic candidate B source: `d3ccb5420cdcaebf73ac9553d32f73c09c899d61`. Windows A executable SHA-256: `a89521cb6ff76916e248b002f253df0c2d5aafb3867d13384d96a498c36622e2`; B: `5d7f2cba9a30de4708a494536770864410454cb8690cdc7e1b38706daec31787`. Both identities were verified by runner execution artifacts.
- Windows saved test revision: `issue247-win-pgr2-equal-store-common@783958562f437def56dad00b8a173bb3c02f35e8a3779917e5e3f45f460e2027`. Vulkan PGR2, same parked race/car and immutable inputs for A and B. Eight runs in **A/B/B/A then B/A/A/B** order. All eight report `execution=completed`, `correctness=passed`, `evidence=complete`, `comparison=eligible`. Start/end screenshots were privately inspected; the visible car and track match. Their SHA-256 hashes are in `atomic-revision-runs.jsonl`; images are not published.
- Segment `stationary-start`. CPU values are xemu process core-percent samples. Frame intervals are guest-frame intervals from the final 25 seconds; cadence is guest flips per second. Summary values are arithmetic means of four per-run statistics per variant, not percentiles of pooled intervals. Windows PGR2 is capped near 30 FPS.

## Windows ordered runs

| Order | Run ID | CPU avg % | CPU p95 % | CPU p99 % | CPU max % | FPS | Frame avg ms | Frame p95 ms | Frame p99 ms | Frame max ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A1 | `20261001-155258584-51f9e10f7f6a4ac6a33e710901def6e7` | 313.15 | 429.88 | 450.89 | 466.92 | 30.000 | 33.333 | 33.498 | 33.899 | 34.831 |
| B1 | `20261001-155437772-2e3cf7eaa7b846ef9aa9e0e4e1a13174` | 306.54 | 393.96 | 424.12 | 435.60 | 30.000 | 33.333 | 33.588 | 33.994 | 34.216 |
| B2 | `20261001-155616536-03d3ca8915bc4794acaaaa0fadb4da7f` | 314.19 | 388.46 | 451.89 | 514.65 | 30.001 | 33.333 | 33.687 | 34.179 | 34.340 |
| A2 | `20261001-155755844-ec474b928a4d4ecead012e798f00d156` | 318.03 | 409.62 | 444.06 | 462.08 | 30.000 | 33.333 | 33.616 | 34.120 | 34.387 |
| B3 | `20261001-155935170-78c8298182504de881c6f8d458ccafc6` | 298.31 | 386.57 | 422.69 | 427.48 | 30.000 | 33.333 | 33.641 | 34.108 | 34.373 |
| A3 | `20261001-160114073-5dc10e47d6b54619afefe5d20cc2a17a` | 317.09 | 426.23 | 453.62 | 462.65 | 30.000 | 33.333 | 33.599 | 33.896 | 34.271 |
| A4 | `20261001-160252895-8f3ce66c5bc9473ca1277163a02b4170` | 325.45 | 436.11 | 518.81 | 533.41 | 30.000 | 33.333 | 33.677 | 33.993 | 34.509 |
| B4 | `20261001-160432354-bd3e9a01c543416e9e5ab1cdc919e997` | 326.73 | 425.12 | 467.30 | 483.70 | 30.000 | 33.334 | 33.688 | 34.106 | 34.424 |

## Mean of per-run statistics

| Metric | A | B | B − A | Improvement |
| --- | ---: | ---: | ---: | ---: |
| CPU average | 318.428 core % | 311.444 core % | -6.984 core % | +2.19% |
| CPU p95 | 425.458 core % | 398.527 core % | -26.931 core % | +6.33% |
| CPU p99 | 466.845 core % | 441.501 core % | -25.344 core % | +5.43% |
| CPU maximum | 481.265 core % | 465.354 core % | -15.911 core % | +3.31% |
| Guest cadence | 30.000 fps | 30.000 fps | +0.000 fps | +0.00% |
| Frame average | 33.333 ms | 33.333 ms | +0.000 ms | -0.00% |
| Frame p95 | 33.597 ms | 33.651 ms | +0.053 ms | -0.16% |
| Frame p99 | 33.977 ms | 34.097 ms | +0.120 ms | -0.35% |
| Frame maximum | 34.499 ms | 34.338 ms | -0.161 ms | +0.47% |

The candidate used **2.19% less sampled process CPU** on average. The 30 FPS cap leaves cadence unchanged. Mean per-run frame p95 and p99 worsened by 0.053 ms and 0.120 ms respectively; mean per-run maximum improved by 0.161 ms. These small frame differences do not establish a frame-time improvement. Candidate B4 used 326.73 core %, above three A runs; it remains in the result.

## Steam Deck incomplete sequence

Deck A/B/B completed with passing correctness but comparison-ineligible uncontrolled Mesa driver-cache state. The fourth run, A2, completed while still on PGR2 Event Select, so its correctness failed. This breaks the requested balanced sequence; no Deck A/B performance comparison is claimed. [Test-runner issue #81](https://github.com/Mainkill1/Xemu-Test-Runner/issues/81) records the guest-visible setup failure. OS controller receipts were present; they do not prove guest consumption.

| Order | Run ID | CPU avg core % | FPS | Frame avg ms | p95 ms | p99 ms | max ms | Status |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| A1 | `20261001-153715288-5eab70177bbb489e945ac0da20160e15` | 314.51 | 18.911 | 51.127 | 69.762 | 87.646 | 181.114 | ineligible cache state |
| B1 | `20261001-154012738-c69891b342cd4991a0bf0d82c184bcf5` | 307.91 | 19.608 | 49.684 | 66.466 | 85.389 | 152.702 | ineligible cache state |
| B2 | `20261001-154310431-9d9ea060736841939d125eeb5977d819` | 310.37 | 18.937 | 51.031 | 67.063 | 90.781 | 185.991 | ineligible cache state |
| A2 | `20261001-154607343-db9337844cb347efb7e1a158d89f1e8b` | — | — | — | — | — | — | correctness failed; Event Select |

## Excluded attempts and limits

- An earlier Windows pilot included a bulk artifact transfer during B1, which the runner correctly marked comparison-ineligible; its later B2 failed before launch because the legacy 2.62 GB HDD copy exhausted disk space. An even earlier preparation attempt failed. These run IDs and exclusion reasons remain in `atomic-revision-runs.jsonl`. After independently authorized cleanup of archived duplicate seed copies, the complete eight-run Windows campaign above ran without tester intervention. [Test-runner issue #47](https://github.com/Mainkill1/Xemu-Test-Runner/issues/47) records the disk failure.
- This revision has only focused local `AddressSpace`/policy tests. Its compare-exchange rejects the staged stale-read interleaving, but QEMU guest direct RAM writers can use ordinary non-atomic stores; a complete cross-writer ordering argument is still missing. No PCM comparison, save/load test, affected APU/audio XISO oracle, or controlled Deck sequence has been completed. Keep PR #276 draft/HOLD.
