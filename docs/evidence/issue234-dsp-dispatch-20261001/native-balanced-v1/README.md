# Deck baseline/candidate campaign: complete, comparison ineligible

**HOLD / draft. All 12 frozen attempts are collected. Nine are canonically eligible; three are ineligible. Neither ABBA nor BAAB qualifies, so native improvement is unavailable.** The failure records and below-floor measurements are retained. No attempt was retried, excluded, waived or repaired.

## What A and B mean

- **A / parent:** fork main `ee5ce48b48784f999af374c1452003f8b2b1230f`, executable SHA-256 `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`.
- **B / candidate:** typed C-DSP handler cache, product source `58df82fe70dfc5ad07cecac974ce84db81559dac`, executable SHA-256 `4e7bf9783374901cb1b81b24d3932f0018900a638a8989ff1ccea0d1f36c5c26`.
- **AAAA:** four parent controls, kept outside local A/B estimates.
- **ABBA:** parent, candidate, candidate, parent. **BAAB:** candidate, parent, parent, candidate. One physical four-run block/order; no inference from pooled host samples or incomplete blocks.

Steam Deck **10.0.0.123**, PGR2 city grid with stationary red coupe, Vulkan, explicitly selected full-C DSP, VP workers 0, 128 MiB and vsync off. Both releases use Clang 21.1.8/Rust 1.96.0 and the exact parent runtime libraries. Candidate-bundle GLib differences were excluded from the causal pair. [Build/library identities](../native-build-identities/identity.json).

Definition `issue234-deck-pgr2-stationary-c-v1`, revision `9f23dbe0bf56df434afd5bdfe571386c8cafd2b93a43989325fc34ba43401dd7`; experiment `issue234-deck-native-c-balanced-v1`. Every launch uses a separate private cold Mesa directory, then 60 seconds scene warmup and 300 seconds observation, unchanged 160 positive interval / 540 CPU sample floors, and no profiler in measured windows. All attempts ran through the maintained HTTP client, physically ordered between 2026-10-02 00:31 and 02:09 UTC. Runner provenance is retained; post-collection version is `0.2.0+848dca74e1ff79f9fc886769a785c23a0945a87e`. Power/frequency, driver RAM caches and OS page caches are uncontrolled.

## All attempts

Host CPU is mean **core-percent**: 100% means one fully occupied host core. Interval columns are **milliseconds between PGRAPH flip-control MMIO events**, not independently verified rendered frame times. Counts below 160 are diagnostic only; runner interval statistics remain unavailable. All attempts passed the declared screenshot correctness check, which establishes visibility rather than image or gameplay parity.

| Physical attempt | Build | Host CPU (core %) | Mean interval (ms) | p95 (ms) | p99 (ms) | Positive intervals | Canonical outcome |
|---|---|---:|---:|---:|---:|---:|---|
| AAAA 1 | A | 198.30 | 1193.45 | 1792.20 | 1931.52 | 252 | Eligible |
| AAAA 2 | A | 204.24 | 1203.95 | 1664.75 | 1765.90 | 250 | Eligible |
| AAAA 3 | A | 199.54 | 1374.53 | 1909.35 | 2268.43 | 219 | Eligible |
| AAAA 4 | A | 199.69 | 1403.42 | 1950.58 | 2113.18 | 214 | Eligible |
| ABBA 1 | A | 200.29 | 1271.34 | 1741.08 | 1922.83 | 236 | Eligible |
| ABBA 2 | B | 193.10 | 1797.58 | 2508.63 | 2912.30 | 167 | Eligible |
| ABBA 3 | B | 193.74 | 1527.34 | 2352.80 | 2609.18 | 197 | QMP quit timeout; ineligible |
| ABBA 4 | A | 195.33 | 1646.57 | 2415.22 | 2667.24 | 183 | Eligible |
| BAAB 1 | B | 202.95 | 1248.42 | 1748.96 | 1844.84 | 241 | Eligible |
| BAAB 2 | A | 197.47 | 1594.49 | 2236.64 | 2413.72 | 189 | Eligible |
| BAAB 3 | A | 185.37 | Unavailable | Unavailable | Unavailable | 115 | QMP quit timeout + incomplete intervals; ineligible |
| BAAB 4 | B | 189.19 | Unavailable | Unavailable | Unavailable | 146 | Completed + incomplete intervals; ineligible |

**Baseline → candidate improvement: NOT COMPARABLE in both orders.** Lower CPU over fixed wall time cannot establish lower fixed-work cost. The four AAAA mean-CPU values span 198.30–204.24 core-percent (range 2.98% of their median); mean intervals span 1193.45–1403.42 ms (range 16.29% of their median). These descriptive ranges are not confidence intervals. Large control variation and differing scene phases limit interpretation even beyond canonical failures.

## Measurement and scene audit

`guest-frames.log` and `guest-flips.log` count `NV_PGRAPH_INCREMENT_READ_3D` control accesses, timestamped with `QEMU_CLOCK_REALTIME`. The runner frame analyzer retains the last 300 seconds relative to the final logged event; its flip analyzer uses the last 40 five-second records. Those windows need not exactly coincide with the host-CPU observation segment. They are host-clock control cadence, not guest time or independently rendered FPS. The frozen profile and full raw logs preserve those distinctions.

All 24 original start/end captures were inspected without editing. Every pair shows the intended red coupe, city grid, 6/6 position and 000 MPH. Starts vary between `GO`, fading `GO`, `1` and `2`; BAAB 3 begins with an oblique prerace camera. Ends show the stationary car and opponents at different distances. [Scene audit](scene-audit.json) records each observation and PNG hash; original captures are in the packet. There is no claim of identical countdown phase, pixels, fixed guest work, PCM parity or continuity throughout the window. The nonblack visibility check does not prove those properties.

[Raw-detail audit](raw-detail-audit.json) reproduces the runner's parsing and final-tail selection. Every cached report equals its collected report after JSON key normalization. Ten qualified frame distributions reproduce; the two below-floor runs have exactly **115 / 146** positive intervals. Their original incomplete outcomes are unchanged.

## Failure classification

- **ABBA 3 (B)** and **BAAB 3 (A)** report `System.TimeoutException: QMP quit exceeded 10000 ms.` The timeout occurs during the declared shutdown step, after observation.
- Both processes were then terminated by the runner: native signal **9**, runner exit code **137**, `RunnerTerminated: true`, `Crashed: false`, no core dump or stack analysis. This establishes a shutdown/control failure on one build each. It does not establish a spontaneous emulator crash, a candidate-only regression, or the reason shutdown became unresponsive. The underlying blockage remains unclassified.
- **BAAB 3 (A)** and **BAAB 4 (B)** lack the required positive interval coverage. BAAB 4 otherwise completed normally with exit 0. No statistics are substituted for the missing runner frame results.

Full `result.json`, crash metadata, stdout/stderr, diagnostics and all ineligible attempts remain in the packet. No timeout was extended and no sample floor lowered.

## Cache result

**All 12** post-exit state reports show `DriverQualification: mesa_private_disk_writes_observed`, `AllowUncontrolledDriverCache: false`, `TargetStopped: true`, and empty `Issues`. This campaign no longer fails the Mesa disk-cache qualification. It does not qualify the separate driver RAM or OS cache states. Raw cache payloads remain local/server-side with hashes for all omitted bytes; no cache payload is needed to inspect the state report or its recorded inventory.

## Evidence and reuse

- [Canonical identities/outcomes and metric-source SHA-256 audit](raw-audit-summary.json): 12/12 collected, 9 eligible, no comparisons.
- [Complete server CSV](server-comparison-full.csv): 100 rows, including unrelated parent-only workloads. All 14 relevant workload-metric rows are `ineligible` with blank before/after/improvement fields. Server build grouping includes AAAA controls; it is not a balanced-order estimator. The local audit keeps controls separate.
- [Raw packet](packet/records.tar.gz), [payload/cache-omission manifest](packet/manifest.json.gz), [archive verification](packet/verification.json): **468 included files**, all payload hashes reread from the finished archive, all 12 attempt directories and frozen drafts/receipts, raw metrics/frame/flip logs, state/config reports, original diagnostic ZIPs, captures, input/runtime/host identities. **9,990 raw runtime driver-cache files** are omitted from publication with their sizes and hashes preserved. They remain local/server-side. No executable, firmware, game image or private HDD is redistributed.
- Retained [collection adapter](collect-native-balanced.py), [comparison auditor](summarize-native-balanced.py), [raw coverage auditor](audit-raw-native.py) and [packet builder](package-native.py) refuse overwriting existing outputs and never launch, retry or repair a measurement. [Audit procedure](AUDIT.md).

Archive SHA-256: `b046a3bf7d334a8761f80ac0cfe07ae4d6fdc51d5950c00b8516b5ee3ccdb99f` (20,140,071 bytes). The Deck was idle with no pending/testing jobs after collection; [status receipt](post-collection-status.json).

## Decision and bounded follow-up

Keep #281 draft. Synthetic mixed/parallel gains remain useful research, while normal/cold regressions and native evidence gaps remain unresolved. First classify shutdown blockage using a separately scoped diagnostic capture and establish scene/work comparability before deciding on another balanced campaign. Do not repeat the failed campaign blindly, relax gates or add diagnostic overhead to measured windows. Audio parity, default-JIT candidate controls and required XISO per-test correctness/timings remain missing. This campaign neither proves a game speedup nor adequately disproves the dispatch direction.

> Agent declaration: evidence reviewed and packaged with Codex (GPT-6).
