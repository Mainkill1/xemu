# Matched original game controls on Steam Deck

**Recommendation: HOLD.** Two original-only PGR2 controls pass the declared preliminary repeatability gate: **0.90% mean-interval range divided by median**, below 5%. This supports proceeding to a balanced comparison; neither run measures the candidate or proves a speedup. Physical ABBA then BAAB has been started separately.

## Results and units

| Original-only control | Mean guest frame interval | p95 interval | p99 interval | Mean process CPU | Correctness / evidence |
|---|---:|---:|---:|---:|---|
| First | 1,676.72 ms | 2,333.79 ms | 2,567.27 ms | 245.44% | Passed / complete / eligible |
| Second | 1,691.88 ms | 2,314.45 ms | 2,464.76 ms | 240.97% | Passed / complete / eligible |

Absolute difference in mean intervals is **15.15 ms**. Positive improvement is not calculated: both are the same original implementation. CPU 100% means one fully occupied logical core. Guest frame intervals are guest-progress observations, not host presentation FPS. These are only two process observations; the 0.90% control spread is not a confidence interval or a guarantee of variation during later runs.

Each stationary segment lasts **301.58 / 301.48 seconds**, preceded by 60 seconds excluded warmup after the unchanged input/loading route. Both processes exit zero, finish all declared steps, and retain their original completed/passed/complete/eligible assessments. Each fully collects **868 artifacts**, zero exclusions. All source analysis is complete with 179 / 178 positive frame intervals. Direct inspection of collected start/end screenshots shows a red car parked at 0 MPH, no GO overlay, and opponents departing. That is scene validation, not audio parity.

Both original controls retain the same stderr warning, `GLib: g_source_destroy: assertion 'g_atomic_int_get (&source->ref_count) > 0' failed`. The processes still exit zero and the runner's declared checks pass. This warning remains an unclassified original-build diagnostic; it was not suppressed or treated as repaired by matching GLib. Candidate stderr must also be reviewed.

Both Mesa reports record private cold disk-cache writes: `DriverNamespaceVerified=true`, `DriverQualification=mesa_private_disk_writes_observed`, `ComparisonReady=true`, `AllowUncontrolledDriverCache=false`. OS page cache and driver memory cache remain uncontrolled; CPU power/affinity was not pinned. Each run uses private HDD/EEPROM and cold application/Mesa disk caches. Prior failed cache assessments and the earlier unstable controls remain visible in their original evidence packets.

## Fixed identity

Steam Deck `10.0.0.123`; HTTP runner `f5b3e58`; Vulkan/full DSP/JIT/128 MiB guest. Original source `ee5ce48b48784f999af374c1452003f8b2b1230f`, rebuilt by CI37004023200 attempt 3; executable SHA-256 `5b6ccf357cfab428e92692b82dc3a5cc75cb50e4065608defbf1cc66b68a597c`. Both controls replace the complete 39-slot executable/library bundle through the saved-test API. The future candidate bundle has identical hashes for all 38 corresponding libraries and matching recorded compiler/release settings; [full build qualification](../issue197-matched-runtime-20261002/README.md).

Retained immutable test: `issue197-deck-pgr2-parked-warm60-300-v3`, revision `3e72921bec480a32d141fe3b8c3894b6a64a864de037cee803d8fde52d7ef919`. Cohort input, runtime package identity, original assessments, full metrics/frame/flip logs, screenshots, scene audits, cache ledgers, collection receipts, and retained HTTP driver are archived. Runs:

- First: `20261002-122829377-4ddd79f0ab21484a822ed87d0a14cf11`.
- Second: `20261002-123703133-827da75722204890b7115604f641f5db`.

## Next comparison and evidence

The candidate comparison uses the same immutable workload and matched bundle qualification in physical **ABBA then BAAB**, four original and four candidate process observations. Review every outcome, scene and measured metric, including regressions and order effects, before claiming a gain. Audio parity and affected/control XISO correctness and per-leaf timings remain separate readiness gates. No PR is marked ready or merged here.

[SUMMARY.json](SUMMARY.json) contains full precision, original canonical outcomes and source-analysis identities. [EVIDENCE.tar.gz](EVIDENCE.tar.gz) includes every non-state collected payload and complete state ledgers. [INDEX.json](INDEX.json) supplies payload hashes and hashes of all fully collected state files retained locally rather than duplicated in the repository. The archive was reopened and every payload verified. Historical live observations are snapshots of earlier stages; completed assessments remain authoritative.
