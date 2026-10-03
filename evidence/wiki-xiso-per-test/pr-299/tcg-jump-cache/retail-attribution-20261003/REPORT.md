# PGR2 reached-scene jump-cache attribution

**Decision: HOLD; test one bounded miss-only eight-entry candidate next.** The
reviewed 300-second stationary race window has materially more recoverable lookup
opportunity than the CPU fixtures. No production victim cache is implemented,
and no performance gain is measured. Diagnostic frame-event cadence is low and
requires matched baseline/resource qualification before interpreting a candidate.

## Scene and procedure

Both actual native captures were viewed. They show the same red car in the city
race scene, gear 1, 000 MPH and position 6/6. The start shows opponents ahead;
the end shows them gone, with moved minimap markers. Neither is a menu, loading,
pause or error screen. Boundary images do not prove every intervening frame.
Their hashes and explicit observations are in `scene-review.json` in the archive.

The saved diagnostic uses explicit `auto_bind=false`, virtual port 1 connected
and keyboard/USB Xbox gamepad binding, timed menu inputs, 60 seconds of excluded
settling, then a **300,000 ms declared wait** with no buttons. This extended
procedure is different from the canonical 30-second course. No approved scene
oracle or baseline was created; generic visibility checks are not scene proof.

`info jit` recipes are declared at both boundaries and do not pause/resume the
guest. The segment runs 05:45:21.524–05:50:23.458 UTC (301.934 s including
captures/control); monitor queries start at 05:45:22.673 and 05:50:23.427 UTC,
300.754 s apart. Counts below are **end minus start**, excluding cumulative boot
totals. Published atomic fields can lag and are not a coherent snapshot.

## What the counters mean

| Window count | Value | Meaning |
| --- | ---: | --- |
| Completed C lookups | 112,278,380 | Sum of primary hit, validated global hit and global miss calls |
| Primary hits | 84,589,975 | Existing direct cache served the C lookup |
| Validated global hits | 27,558,622 | Primary missed; real global lookup found an existing valid TB |
| Global misses | 129,783 | Real global lookup did not find an existing TB |
| Empty-slot global hits | 16,684,773 | No primary TB pointer was observed before global hit |
| Different-PC global hits | 9,928,597 | Occupied primary slot contained a different virtual PC |
| Same-PC/state global hits | 945,252 | Primary full-key validation failed with the same virtual PC |
| CF_PCREL whole clears | 121,736 | Original clearing behavior remains |
| New TBs / recycled TBs | 3,539 / 121,731 | Recorded code-generation paths, not clear-caused retranslation proof |
| Shadow resets / overlapped fills | 108,958 / 0 | Conservative invalidation resets and excluded publication overlaps |

| Independent FIFO model | Potential recoveries | % of validated global hits | % of completed C lookups |
| --- | ---: | ---: | ---: |
| Eight entries | 5,620,784 | 20.40% | 5.01% |
| Sixteen entries | 7,836,701 | 28.44% | 6.98% |

These percentages are **not Improvement %**. Each recovery matches virtual PC
and opaque displaced TB token against the real validated global result. Tokens
never execute, get dereferenced or populate the real cache. All invalidations
discard model history. Generated fast lookup/direct chaining bypass C counts.
The extra sixteen-entry recoveries do not prove that doubling a real cache wins.

The [CPU attribution packet](../REPORT.md) has eight-entry recoveries of only
0.01271%/0.03015% of completed C lookups in its coarse stable/rewrite windows.
This reached retail window supports a bounded experiment more strongly; it does
not establish miss cost or time saved. The
[controlled observer matrix](../corrected-observer-cost-20261003/REPORT.md)
already shows intrusive diagnostic costs, including 8.38% slower rewrite work in
conflict mode. Do not compare instrumented timing against uninstrumented builds.

## Diagnostic resource observations and limits

The maintained analysis records 605 host process CPU samples, mean **255.65% of
one core**, and zero sampler overruns. Its final 300-second frame-log tail has
**176 intervals**, mean **1,706.20 ms**, p95 **2,419.63 ms**, p99 **2,609.09 ms**.
These are logged PGRAPH increment events, not proof of presented/displayed FPS.
The flip summary covers only its final six aggregate samples (18 events over
34.324 s); it is not the full 300-second interval. Full sources/statistics remain.

Baseline time, time saved and Improvement % are **not established**. This is one
instrumented run without A/A or ABBA/BAAB game comparison. Neither the low event
cadence nor its cause is classified. Exit code is zero, but stderr contains
`g_source_destroy`'s GLib reference-count assertion at its end; retain it as an
unclassified warning, not a warning-free qualification.

## Exact identities and evidence

- Steam Deck `10.0.0.123`, Vulkan/RADV, 128 MiB, full DSP/default JIT, VP workers 0,
  vsync off. Maintained LAN HTTP only; no intervention or external sampler.
- Product `31c083ece2e7d44e87b2c7c4039cfc6b2365848d`, GCC14 O2 diagnostic build.
  Executable SHA-256
  `db2edddae3080e26f166b4e9750d28449e1150604b20f9135aa884cebb3a5d58`.
- Runner `c264004dfc906eef008c8a7235764c37daee330b`, including current #93/#94/#95 fixes.
- Saved test `i167-pgr2-conflicts-300-v1`, revision
  `9887371b436b67d9466581ea545274f4908e7360fead533ef991e4eac3ac7a6b`;
  request `i167-pgr2-diag-001-t001`; run
  `20261003-054249387-64aea7bda0da46b4820fc3b8367190ff`.
- Native execution completed; declared checks passed; evidence complete and
  individually eligible. This is not approved benchmark/game correctness status.
- Private cold Mesa disk namespace started empty, ended with 833 files /
  3,864,274 bytes, and qualified with observed writes, zero issues, no waiver/purge.
  OS page cache and driver memory cache remain uncontrolled.

All **871 native files** were collected with **zero exclusions**: 38 public files
and 833 private runtime/cache files individually hashed and retained locally.
[INDEX.json](INDEX.json), [SUMMARY.json](SUMMARY.json) and
[EVIDENCE.zip](EVIDENCE.zip) preserve raw snapshots, both captures, complete
analysis/logs, frozen job/configuration and asset identities. A configuration
description exceeded the 240-character limit and was rejected before execution;
the receipt is preserved. No native retry, reference pin or selfapproval occurred.

## Next experiment

Keep default production dispatch uninstrumented. Audit every mapping/code/lifetime
clear and storage-reuse path before implementing a miss-only eight-entry cache.
Preserve full-key/CF_INVALID rejection, virtual remapping and spanning pages,
physical SMC, full flush/reclamation, reset/load, late publication and concurrent
invalidation. Retain meaningful tests and qualify a matched production baseline
with A/A, physical ABBA/BAAB, original XISO references and reached-scene/resource
controls. No useful measured gain after adequate evaluation means close with
findings; these counts alone do not justify readiness. Keep PR #299 draft.
