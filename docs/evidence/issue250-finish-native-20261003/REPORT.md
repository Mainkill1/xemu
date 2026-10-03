# #250 current-main PGR2 native finish diagnostic

## Decision

**HOLD. One attempt, retained unchanged.** The production-main executable reached
PGR2's Hong Kong race scene and completed the 300-second observation. The
runner failed the required frame-sample evidence floor; this is an ineligible
performance measurement, not a passing candidate. This branch retains a
maintained offline reader and the actual reusable recipe. There is no resource
retirement, renderer, guest-clock, PTIMER or timeout change and no speedup claim.

Private cold **Mesa qualification passed** without waivers. That storage gate
is separate from the failed frame evidence. No cache was purged, failed sample
replaced, floor lowered or runtime behavior modified to obtain a pass.

## Quick qualification table

| Gate | Recorded result | Interpretation |
|---|---|---|
| Execution / declared visibility | Completed / passed | Exit 0; plan completed, images nonblack. Not emulation parity. |
| Direct scene review | Race reached; player at 0 MPH in all three captures | Stationary diagnostic, not driving/streaming pressure. |
| Declared observation | 302.7241512 s including capture steps, after 60 s settling | Not identical to a control-frame telemetry tail. |
| Frame evidence | Incomplete: insufficient positive frame intervals | 160 interval floor retained; no replacement run. |
| Telemetry trailing 300 s | 108 complete control buckets; 109 positive guest-frame intervals | First/crossing buckets are separate. |
| Monitoring | 606 samples; mean process CPU 244.842%; zero overruns | Core-summed host CPU utilization, not a speedup or comparison. |
| Last six flip intervals | 13 flips / 35.570917 s = 0.365467 flips/s | Recorded guest cadence; not a baseline/candidate delta. |
| Cold private Mesa | Complete; namespace verified; private disk writes observed | Runner storage comparison gate passes, overall comparison ineligible. |
| Branch CI at `2dcfd758e7d674ac868a4788e65b1b7388cf1976` | All 40 checks completed success, including both unit jobs | New commit retains the identical reader/tests and production tree. |
| Fresh local reader checks | 24/24 pass | Offline analysis correctness, not emulator qualification. |

## Finish attribution

| Finish reason | All-log calls / submits / timed | All-log sampled fence wait, ms | Complete final-300-second submits / timed | Final-300-second sampled fence wait, ms |
|---|---:|---:|---:|---:|
| vertex_buffer_dirty | 0 / 0 / 0 | 0.000 | 0 / 0 | 0.000 |
| surface_create | 93 / 77 / 36 | 14.793 | 0 / 0 | 0.000 |
| surface_down | 108 / 39 / 39 | 124.160 | 0 / 0 | 0.000 |
| need_buffer_space | 448 / 448 / 448 | 1785.359 | 189 / 189 | 524.040 |
| framebuffer_dirty | 0 / 0 / 0 | 0.000 | 0 / 0 | 0.000 |
| presenting | 3508 / 3508 / 3508 | 2963.804 | 108 / 108 | 84.826 |
| flip_stall | 3777 / 2443 / 2443 | 4815.008 | 108 / 108 | 126.240 |
| flush | 1 / 0 / 0 | 0.000 | 0 / 0 | 0.000 |
| stalled | 131 / 131 / 131 | 183.146 | 87 / 87 | 111.124 |
| texture_dirty | 0 / 0 / 0 | 0.000 | 0 / 0 | 0.000 |

These are exact recorded counts and **sampled host elapsed fence-wait
milliseconds**, not CPU time, GPU execution or removable cost. `surface_create`
all-log duration covers only 36 of 77 submissions; the rest must not be
extrapolated. The nonzero other rows above have all submissions timed in their
scope. All-log values include boot/navigation/settling; the complete tail has
108 buckets and is not exactly the declared measurement segment.

The complete trailing 300-second buckets contain 189 `need_buffer_space`
submissions and **524.040 ms** of sampled wait, plus 27.268 ms of sampled
submission time. `vertex_buffer_dirty` remains zero. The boundary-crossing
bucket separately contains one buffer-space submission/2.477 ms wait and is
not credited to complete-tail totals. Activity after the final record is
unobserved. This trace cannot establish a large #250 benefit or explain the
very low cadence. Nested renderer CPU regions cannot be added to compute
missing time; a separate host profile is needed before assigning a cause.

## Source ownership check and bounded direction

`need_buffer_space` is shared by multiple production paths: pipeline LRU
exhaustion (`draw.c`), framebuffer limits (`draw.c`), vertex/index/uniform/
texture staging pressure (`draw.c`, `shaders.c`, `texture.c`), buffer allocation
growth (`buffer.c`) and compute conversion conflicts (`surface.c`, `texture.c`).
The reason aggregate does not identify which path caused a submission. A
buffer-slot proposal cannot assume every event belongs to a buffer.

Existing `XEMU_VK_HYBRID_TRACE` can classify some shortages without adding a
new probe, but was **not enabled in this attempt**. Its production implementation
retains a 16,384-event ring, writes only frames >=50 ms, records dropped events
and forces finish timing. It also omits shortage markers at some existing
call sites; absence of a marker would not prove absence of pressure. Any use
must be an explicitly instrumented diagnostic with its overhead/coverage
reported. First profile the slow reached scene; pursue a bounded ownership
prototype only if material non-observing critical-path waits are established.
Preserve the measured guest-observable flip/report waits.

## Identical recipe, assets and native provenance

- Host: Steam Deck `10.0.0.123`, AMD Custom APU 0405, Vulkan/RADV VANGOGH.
  GL presentation reports Mesa 25.3.0; Vulkan reports driver 25.99.99. These
  strings are retained verbatim in `stderr.log`, not normalized into a guessed
  common version. Full DSP/default JIT, VP setting 0 (automatic; eight workers on Deck), 128 MiB, fixed keyboard
  binding/virtual port, cold private application/driver state and cloned HDD.
- Product: main `76c23c7d444a6f12c9778bb2c35fab513f6c8056`, executable SHA-256
  `5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b`.
  Runtime log and input manifest agree; all six declared input hashes verified.
  Existing Vulkan telemetry enabled, TCG/hybrid observers disabled.
- HTTP runner `0.2.0+e17919c849b840d43171a615b288f559fbec9e4c`; application
  `i250-76c23c7d-pgr2-finish-001`; request `i250-finish-native-20261003-001-t001`;
  run `20261003-155542759-ab6ec0c1fba545c3a720fdf81dc8ee15`.
- Saved test `i250-pgr2-finish-300-v1`, revision
  `6a75b0ba5ffef2b35ffbbc10e9202e0f6d7740ed02fe4f120f4a2c56382f0135`.
  The exact authored job/configuration/contract and reusable workflow live in
  [the maintained recipe](../../performance/recipes/pgr2-finish-300/README.md).
- Reader source `3e69f0b8f11`; SHA-256
  `0f2a1b13d567ce183c3a341989c0071f9e218c8f85a8ee9bd2e613a6a20b84da`.
  Production renderer/finish sources remain identical to measured main.
- Final raw telemetry: 14,529,127 bytes; 3,777 records; timestamp span
  447.519068 s; SHA-256
  `1c8545c6cb146bdf06abfe796b2cecd3a55f7abed8ab82fdb4c805b42b57c1a2`.
  Every offline report is generated from this stopped file and created without
  overwriting prior output. Earlier #246 and #250 traces remain separate.

## Preserved failures and observer limits

The runner originally reports `completed / passed / incomplete / ineligible`.
`analysis:frames` fails with `Insufficient positive frame intervals`. Generic
image checks pass but do not supersede that failure. Direct viewing confirms
GO/rivals at start and rivals farther ahead at midpoint/end; all images retain
the stationary player. No manual input, pause, preview, active transfer or
host-state change occurred. One declared non-pausing header query after the
segment is recorded as diagnostic/operator activity; do not relabel it absent.

Shutdown stderr records
`GLib: g_source_destroy: assertion 'g_atomic_int_get (&source->ref_count) > 0' failed`.
The runner reports exit 0/no crash. Both facts remain visible; there is no
clean-shutdown or regression-free gameplay claim. No cause is assigned without
profiling. Driver memory cache, OS page cache, scheduling/power, shader warmup,
instrumentation and screenshot overhead remain disclosed limitations.

The local saved-definition verifier initially rejected server-added permission
metadata before execution; its corrected key-level verification is retained
in `definition-verification.json`. Only the one above native attempt started.
HTTP collection after idle retrieved **873/873 eligible artifacts**, with zero
excluded by the runner. All are retained locally and hashed. Public ZIP omits
only binary cache contents under `state/`: their complete per-file hashes and
original Mesa state ledgers remain public; no test sample/assessment/log/capture
is omitted. Retail disc, firmware, guest HDD and executable are not published.

[Complete summaries/provenance](SUMMARY.json) · [Raw logs, captures, original
assessment and checks](EVIDENCE.zip) · [Public payload hashes](INDEX.json).
Evidence belongs to xemu draft PR #302, not the runner or test-tool repository.
No A/A/ABBA/BAAB delta exists for this unpaired diagnostic. Runtime performance
for the offline source delta is N/A; any future emulator candidate requires
matched builds, fresh A/A, balanced comparisons and affected/control XISO
correctness/timings before reconsideration.
