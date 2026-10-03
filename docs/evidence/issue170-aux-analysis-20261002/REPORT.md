# Auxiliary Vulkan upload opportunities in retained Deck traces

**Decision: keep #170 open; do not implement a retirement ring from these captures.**
The parked PGR2 and Morrowind name-entry captures have no eligible auxiliary
upload/create submissions in their final recorded 60-second windows, including
the separately reported boundary buckets. The remaining observed waits are
presentation and CPU readback. Neither is a #170 optimization candidate.

## Results at a glance

These are existing diagnostic captures, not a baseline/candidate performance
campaign. No runtime optimization or speedup is implemented. There is no
candidate time, saved time or Improvement % to report.

| Auxiliary caller | PGR2 whole-log calls | PGR2 queue wait ms | PGR2 tail calls | Morrowind whole-log calls | Morrowind queue wait ms | Morrowind tail calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Surface create | 88 | 36.345 | 0 | 24 | 14.239 | 0 |
| Surface upload | 93 | 75.677 | 0 | 28 | 47.489 | 0 |
| Texture upload | 0 | 0 | 0 | 0 | 0 | 0 |
| PVIDEO upload | 0 | 0 | 0 | 0 | 0 | 0 |
| Dummy texture create | 1 | 0.380 | 0 | 1 | 0.377 | 0 |
| Surface download, CPU readback | 930 | 234.626 | 486 + 6 boundary | 23 | 31.198 | 0 |
| Display render, presentation | 6471 | 1711.997 | 81 + 1 boundary | 14329 | 4104.324 | 2995 + 2 boundary |

Queue wait values are measured host elapsed time around vkQueueWaitIdle, not
CPU execution time, GPU service time or avoidable cost. The existing single-time
helper measures every auxiliary submit/wait; the sampled finish telemetry is a
different lane. Do not add nested CPU-region totals or predict FPS from these
durations.

## Source and workload applicability

Main is ee5ce48b48784f999af374c1452003f8b2b1230f; measured collector runtime is
8079502ae25a602e1395fa9ee87ec1ef8b57c9d6. command.c, perf.c, surface.c and display.c
are byte-identical between those trees and this publication. The source hash
receipt is in SUMMARY.json. The texture VMA wrapper differs on the collector
branch; this is instrumented diagnostic evidence, not normal-build timing.

Both captures ran on **Steam Deck 10.0.0.123**, Vulkan/full DSP/default JIT,
maintained HTTP runner 848dca74. Executable SHA256:
1b52adf0dea0823a95c37ae79d447af01bf8a904d6385037908b2ca57d44bba2.
The pinned parent library bundle is retained in the original manifests;
candidate packaged GLib differed. Private Mesa disk qualification passed without
waivers. OS/driver RAM caches and power/affinity remain uncontrolled.

PGR2 is the Hong Kong parked starting grid at 0 MPH, with GO then other cars
departing. Morrowind is the initial ship interior behind its name-entry modal.
Neither covers sustained driving/streaming or repeated resource transitions.
The original PGR2 collector is ON; the selected Morrowind run is the same
executable with allocation collection OFF. They are different workloads, not
an overhead comparison. Earlier timeouts, PFIFO aborts and rejected driving
coverage remain in the existing PR293 evidence.

PGR2's 3,704 control-frame records span 163.876093 seconds; Morrowind's 7,550
span 231.077884 seconds. These short historical diagnostics do not satisfy the
current five-minute PGR2 performance-acceptance requirement. No new native job
is launched, and no historical run is reclassified as a qualified game campaign.

## What the maintained reader adds

The existing scripts/performance/vk-perf-summary.py adds --auxiliary and
--tail-seconds. It validates the seven schema-8 caller names, contiguous
control-frame records, monotonic clocks, nonnegative arrays and the actual
submit/timed-submit/wait invariant. Unknown names, missing counters,
contradictory counts, short coverage and nonpositive windows fail closed.

Per-frame counters cover the interval between consecutive record timestamps.
The bucket crossing the requested start is separate; it cannot locate events
within that interval. Complete PGR2/Morrowind tail buckets number **81 / 1547**.
The first bucket's start and activity after the final record are unobserved.
These control-frame labels do not establish displayed FPS. Tail buffering is
bounded by the requested time span, not a hard record-count/memory ceiling.

CPU-region and live-header behavior remain unchanged. Both offline readers
are now registered in the Meson unit suite, which the existing CI runs.
Reproduce from the extracted packet and retained source script:

```sh
python3 source/scripts/performance/vk-perf-summary.py   inputs/pgr2-v1/vk-perf.jsonl --auxiliary --tail-seconds 60
```

## Verification and failures

The original 11 reader checks pass before the change. New functionality first
fails the missing-API assertions, then passes the literal counter/window and
damaged-input checks. The final registered run passes **15 telemetry checks
and 11 allocation-reader checks**. A deliberately incorrect exact-boundary
partition loses a complete bucket and is rejected by the retained test.
Independent source-field summation and a separate read-only review reproduce
both native summaries, hashes and byte sizes. Full archive payload hashes are
verified after writing.

The full local unit build still fails in unchanged test-xbox-mcpx-apu-resampler.c:
GCC14 rejects missing sinf/cosf declarations. A Python discovery invocation
finds zero tests because the scripts use hyphenated filenames; this is not a
passing suite. The direct and registered reader runs execute the tests.
All logs, including those failures, are retained. No full local-suite success
or new CI success is claimed by this packet.

## What remains before a runtime experiment

Keep PR293 draft/HOLD and #170 open. The missing scope is actual upload-heavy
driving/streaming or repeated transitions, then attribution of an eligible
caller before an ownership/retirement change. A zero count in these windows
does not prove all titles lack an opportunity. Preserve report/readback
completion and contribute display ownership work to #90. No new runtime
XISO comparison is claimed; affected/control references and per-leaf timings
remain required if a runtime candidate is developed.

[Complete packet](EVIDENCE.tar.gz), [payload hashes](INDEX.json),
[source and native provenance](SUMMARY.json).

Archive SHA256: `f27af108377cff7a07c3e9523f48bd683a2a794a62e606b81d6792cfbfd44f4f`.
