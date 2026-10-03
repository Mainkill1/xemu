# Issue197: controlled Deck sample-memory reader measurements

## Decision

**Keep xemu PR275 draft / HOLD.** The production encoded-word reader costs substantially less in the controlled component fixture on Steam Deck `10.0.0.123`. Audio/game qualification and broader concurrent memory/register coverage are still required. No measured FPS or whole-emulator gain is claimed.

## What the labels and units mean

- **A / original:** the existing per-word SGE descriptor and physical payload loads, retained as an explicit reference loop in the integration executable.
- **B / candidate:** the private production `sample-memory.h` reader, with bounded RAM cache spans and a per-word FlatView/descriptor check.
- **Mono / 9 words:** one 36-byte encoded ADPCM block. **Stereo / 18 words:** one 72-byte encoded ADPCM block. This fixture measures encoded-word reads, not decode, resampling, sound quality, or frames.
- Each process warms 4,096 blocks outside timing and reads **10,000,000 blocks** inside the monotonic-clock interval. All output words contribute to an independently calculated checksum.
- `elapsedUs` is **microseconds for all 10 million blocks**. Tables convert it to seconds. Improvement is `100 × (original median − candidate median) / original median`; positive means less time.
- Each workload executes four original-only controls, then **A B B A**, then **B A A B**. Every letter is a separately launched/archived native process. Four original and four candidate observations enter the combined table; original-only controls are excluded.

## Results

| Encoded block | Original median (s / 10M blocks) | Candidate median (s / 10M blocks) | Time saved (s) | Improvement % | n A / B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mono: 9 words | 5.020494 | 2.608690 | 2.411803 | +48.04% | 4 / 4 |
| Stereo: 18 words | 9.686030 | 4.422021 | 5.264010 | +54.35% | 4 / 4 |

| Encoded block | Physical order | Original median (s) | Candidate median (s) | Improvement % |
| --- | --- | ---: | ---: | ---: |
| Mono | ABBA | 5.017404 | 2.606707 | +48.05% |
| Mono | BAAB | 5.024713 | 2.610686 | +48.04% |
| Stereo | ABBA | 9.686212 | 4.426940 | +54.30% |
| Stereo | BAAB | 9.671860 | 4.417349 | +54.33% |

| Original-only controls | n | Median (s) | Minimum–maximum (s) | Spread / median |
| --- | ---: | ---: | ---: | ---: |
| Mono | 4 | 5.019537 | 5.010003–5.034504 | 0.49% |
| Stereo | 4 | 9.678548 | 9.650575–9.695922 | 0.47% |

These are descriptive medians from four observations per mode, not confidence bounds or a significance test. CPU affinity/power policy were not pinned. The captured host inventory records SteamOS, AMD Custom APU0405, eight logical processors, and no reported power scheme. Matched physical orders and the original-only controls reduce ordering uncertainty; broader workloads remain necessary.

## Correctness, identity and measurement boundaries

- **24/24 timing processes** completed, exited zero, passed output checks, archived, and collected completely with zero excluded artifacts. All 24 indexed timing records are eligible with no issues. Their server `encoded-read-time` values match raw stdout exactly.
- The two dedicated native correctness processes each pass three actual-QEMU memory cases: replacement RAM at unchanged physical address, stable mono, and stable stereo. Their canonical workload outcomes are passed/complete/eligible. Build-history indexing correctly records `metrics_missing` for those correctness-only processes; they have no timing metric and are excluded from the timing table.
- Ten-million-block checksums: mono **191790886280000000**; stereo **386189776490000000**. The fixture calculates expected values separately. Assertions also validate every word in the warmup.
- One exact executable and its corresponding extracted AppImage libraries are used throughout. SHA-256: `71feecec2e29001bff791fbcd1d8d90361daead26b35fac8aa7b54239d625ac6`; ELF Build ID: `31ee221565887d38329f209c7df46af8a894c5ba`. The full 39-file payload manifest pins libraries. No executable stripping or library substitution.
- Artifact: xemu CI36993825114, release Linux x86_64, build revision `80932c543b9b`. The downloaded CI source archive exactly matches product/test head `e25b0ba239c8146d2d661f26ec5ad29e82a14853` for the production reader, integration fixture and `vp.c`; `source-equivalence.json` preserves hashes.
- Corrected xemu CI passes all native/platform jobs, 137 ordinary unit checks, the separate sample-memory API-double target (nine subtests), and candidate/original integration targets (three subtests each) in production Linux builds. Earlier CI configuration failures and the local GCC14 resampler compile failure remain preserved in the preceding real-memory packet; successful CI does not rewrite those outcomes.
- This is a **same-executable runtime-mode experiment**. The two mode arguments intentionally define different saved workload keys. It is not a cross-build `/compare` cohort or a whole-game qualification. Tables use the indexed per-run measurements, not reanalysis of metrics CSV.
- The benchmark initializes actual QEMU RAM/address-space objects without starting graphics, a game, or audio. **Mesa is not used**; the process contract has no xemu/Mesa state qualification. This does not fix/waive a game cache failure. Earlier game controls retain their actual qualified private cold Mesa evidence and unstable scene results.

## Maintained execution and retained tests

Runner [draft PR89](https://github.com/Mainkill1/Xemu-Test-Runner/pull/89), revision `f5b3e58de71ce6fb56450679a456fd049aa95b29`, supplies explicit process targets through maintained HTTP clients. Linux/Windows CI and all related runner workflows pass. Deck operator upgrade retained the previous binary and unchanged runner configuration SHA-256 `8d0265ba728e1f01100a9c579ef9c888abdb5053a12b7277ba7f3eba34b0bd60`. SSH was used only for this upgrade; tests, waits, state/results, archival, collection, indexing and saved definitions used LAN HTTP. Deck ended idle with empty Pending/Testing queues.

Four immutable saved tests retain the exact executable/library build slots and workload contracts for future HTTP runs:

| Saved test ID | Immutable revision |
| --- | --- |
| `mcpx-adpcm-real-ram-mono-baseline-v1` | `b983471b82ee330d6913f0b0049658748a7f7a961933b1f746fd9a4a95f1fa04` |
| `mcpx-adpcm-real-ram-mono-candidate-v1` | `8f03686904ae104efcc50dedddc1e37466faa5beaae83c62f02cfa12884e66c7` |
| `mcpx-adpcm-real-ram-stereo-baseline-v1` | `e2f73252115888b62133b9c675a1452408447d5c0f61633d501b3dd0b336581a` |
| `mcpx-adpcm-real-ram-stereo-candidate-v1` | `a240678033e19080f38f9fd2c9877583d19c7162f86d66407fe32182267a361e` |

Use the maintained `runner_api.py run TEST --revision FULL_REVISION --id NEW_ATTEMPT` workflow. The source integration/benchmark target remains in `tests/xbox/mcpx-apu/test-sample-memory.c`, Linux CI retains its executable, and the process feature/fixtures live in runner PR89. No evidence-only runner commit.

## Preserved helper failures

The campaign helper initially parsed TAP correctness output as JSON and stopped before timing began. Its archived original control was reused without re-execution after the parser repair. A saved-test bake requested during an active benchmark received HTTP409 `operation_blocked`; no mutation/tool launch or policy bypass occurred. Saved tests were published only after the completed campaign was idle. Those helper events, logs, and all original assessments are retained. Earlier runner regression failures and subsequent clean test logs are included.

## Packet and remaining work

`EVIDENCE.tar.gz` contains all original assessments, jobs, launch/input identities, host inventory, raw output/telemetry, complete collection manifests, server index records, saved-test receipts, frozen physical order, helper logs and CI/source identity evidence. `INDEX.json` pins every archived file and explicitly omits the duplicate executable/library package, retained locally/server-side and pinned by CI/hash. The packet was reopened and every file hash checked. `SUMMARY.json` retains full-precision values.

Remaining: affected ADPCM audio parity, controlled game throughput/frame tails, affected/control XISO correctness and per-leaf timings, and broader concurrent mapping/register behavior. The earlier PGR2 same-parent scene variance blocks game-level conclusions. PR187 stays deferred; no new Windows box run; no merge.
