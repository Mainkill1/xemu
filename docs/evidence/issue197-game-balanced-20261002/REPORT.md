# Issue #197: matched Steam Deck ABBA / BAAB game evidence

**Decision: HOLD.** All eight native processes qualify, but this game cohort does not establish a reliable speedup. The combined median is 7.61% lower for the candidate; large variation and contradictory adjacent pairs prevent treating that as a gain. The separately proven component reduction remains useful.

## Comparison and controls

A = unchanged original `ee5ce48b48784f999af374c1452003f8b2b1230f`; B = candidate `c4cdef6cad3dd22d516e16ec2cb1aa9c06ec11f6`. Production reader/caller match the component-tested `e25b0ba` and current documentation-only head. Original/candidate SHA-256: `5b6ccf357cfab428e92692b82dc3a5cc75cb50e4065608defbf1cc66b68a597c` / `95f81f33d932cb297d6f637eb0635ac2faa2b895d330bae6871e3012d6353667`. All 38 corresponding libraries, Clang/LLD 21.1.8 and recorded release options match. `builds/identity.json` binds the complete 39-slot bundles.

Steam Deck `10.0.0.123`; maintained HTTP runner `f5b3e58` at port 9368. Eight separate cold-start processes, physical **ABBA then BAAB**, no retries. Same immutable PGR2 parked workload `issue197-deck-pgr2-parked-warm60-300-v3`, revision `3e72921bec480a32d141fe3b8c3894b6a64a864de037cee803d8fde52d7ef919`: Vulkan, full DSP, JIT, 128 MiB, surface scale 1, VP workers 0, no vsync, identical input; exclude 60 seconds of warmup, then collect at least 300 seconds parked. All eight start/end captures directly inspected: red car at 0 MPH, no GO overlay. Private HDD/EEPROM and cold application/Mesa disk namespaces; all eight observe private Mesa disk writes without waiver. Host power, affinity, OS/page cache and in-memory driver state were not pinned.

Two preceding matched original-only controls had a 0.90% range/median. That preliminary result did **not** predict the variation observed during the balanced campaign. No measurements were discarded to improve the result.

## Quick results

These are **guest frame-progress intervals**, not host presentation frame times or game FPS. Each cell is the median across per-process statistics. Positive Improvement % = less time: `100 × (A − B) / A`.

| Metric | Original A | Candidate B | Saved | Improvement % |
| --- | ---: | ---: | ---: | ---: |
| Mean guest interval | 1799.41 ms | 1662.47 ms | 136.94 ms | +7.61% |
| p95 guest interval | 2542.73 ms | 2312.68 ms | 230.05 ms | +9.05% |
| p99 guest interval | 2757.69 ms | 2463.72 ms | 293.97 ms | +10.66% |
| Mean process CPU | 244.66 % | 245.05 % | -0.39 % | -0.16% |

CPU 100% is one logical core. CPU falls/increases are not normalized-work proof; the combined change here is an increase of 0.39 percentage points.

| Physical order | A median (ms) | B median (ms) | Saved (ms) | Improvement % | A range / median | B range / median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ABBA | 1774.64 | 1504.19 | 270.45 | +15.24% | 17.08% | 26.39% |
| BAAB | 1876.85 | 1688.00 | 188.85 | +10.06% | 21.76% | 7.79% |
| combined | 1799.41 | 1662.47 | 136.94 | +7.61% | 25.45% | 26.95% |

| Adjacent process pair | A (ms) | B (ms) | Improvement % |
| --- | ---: | ---: | ---: |
| 1 / 2 | 1623.12 | 1702.67 | -4.90% |
| 3 / 4 | 1926.17 | 1305.72 | +32.21% |
| 5 / 6 | 1672.65 | 1753.73 | -4.85% |
| 7 / 8 | 2081.04 | 1622.26 | +22.05% |

## Every process

| Position / build | Mean (ms) | p95 (ms) | p99 (ms) | CPU (%) | Positive intervals | Run ID |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 / A | 1623.12 | 2258.09 | 2459.32 | 246.24 | 185 | `20261002-124653752-b0deef076f4b487882b6aeef70c071a5` |
| 2 / B | 1702.67 | 2279.03 | 2477.57 | 245.10 | 177 | `20261002-125526959-20e312b66b884ef7978a49c72bf82efd` |
| 3 / B | 1305.72 | 1747.14 | 1855.18 | 248.69 | 230 | `20261002-130400428-104c3e1df62a4648a9f392ce97306f42` |
| 4 / A | 1926.17 | 2871.40 | 3086.03 | 242.91 | 156 | `20261002-131234310-f45cd6fd3c5748f4990440aaf1e56c9a` |
| 5 / B | 1753.73 | 2346.34 | 2452.63 | 245.01 | 172 | `20261002-132107870-4ae56be644144148bc4708f2131d6cb7` |
| 6 / A | 1672.65 | 2288.87 | 2413.77 | 246.21 | 180 | `20261002-132941689-17dfa344dfb8483596a484e444beb0ce` |
| 7 / A | 2081.04 | 2796.59 | 3056.07 | 243.11 | 145 | `20261002-133814761-7da14003b75045b1adefa063d3c1a917` |
| 8 / B | 1622.26 | 2347.67 | 2474.81 | 243.64 | 185 | `20261002-134648336-d961aa405ccc4c7ead0825b987f0330f` |

## Verification and limits

- All eight: execution completed, declared correctness passed, evidence complete, comparison eligible, exit zero; **868 files each**, complete collection, zero exclusions. Eligibility alone does not establish repeatability, audio parity or speedup.
- Independent helper verifies source sizes/hashes, exact 300-second frame windows, monotonic timestamps/frame counters, CPU stationary segment, means and linear p95/p99 against every archived report. Receipt: `independent-statistics-complete.json`, eight verified runs. Earlier partial receipts are preserved.
- All eight retain the same unclassified `GLib: g_source_destroy` reference-count assertion warning seen in matched original controls. This was not hidden, repaired in analysis or classified as a patch regression. Full stderr is archived.
- No confidence interval or significance claim from four processes per build. Within-build range greatly exceeds the apparent combined effect; adjacent pairs differ in sign. Do not use the favorable combined or order medians as a game-gain acceptance gate.
- Native audio parity, affected ADPCM and unaffected PCM XISO checks, valid per-leaf performance contracts, and arbitrary RAM backing destruction/resize remain unqualified. A retained guest fixture is being developed separately; its host-paced progress wait will be labeled **NOT COMPARABLE** for throughput.

## Retained evidence

`SUMMARY.json` contains full-precision outcomes/statistics/order checks. `INDEX.json` verifies every archived payload and separately hashes collected guest/cache state retained locally. `EVIDENCE.tar.gz` contains all non-state collections: logs, screenshots, reports, ledgers, observer log, frozen build identities, scene audits, and reusable collection/recalculation helpers. No bulk artifact was excluded from native collection; published state omission is explicit and does not change original outcomes. Earlier failed/ineligible controls and CI attempts remain in their earlier xemu evidence directories.
