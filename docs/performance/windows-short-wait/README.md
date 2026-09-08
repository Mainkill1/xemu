# Windows short-wait investigation

Status: **held; qualification incomplete**. This records completed diagnostics,
not an accepted XISO speedup or a complete PGR2 lag fix. Related work:
[issue 19](https://github.com/Mainkill1/xemu/issues/19) and
[active PR 25](https://github.com/Mainkill1/xemu/pull/25).
Historical research remains in [PR 24](https://github.com/Mainkill1/xemu/pull/24).
[Branch archive and consolidation record](branch-archive.md).

## Retail: same executable, setting off/on

All eight runtime cells completed on Release implementation `0edb216c23`:
one 60-second capture per workload/mode/renderer. Morrowind used the fixed
snapshot and unchanged Start/B sequence; PGR2 used the existing full fresh-start
sequence. Seeds remained unchanged and private-HDD cleanup passed. These single
captures do not establish statistical non-inferiority or a broad lag fix.

| Workload / mode | Cadence FPS | p95 ms | p99 ms |
|---|---:|---:|---:|
| morrowind-off-opengl | 38.543 | 32.023 | 36.845 |
| morrowind-off-vulkan | 27.919 | 42.710 | 49.049 |
| morrowind-on-opengl | 38.544 | 31.768 | 36.909 |
| morrowind-on-vulkan | 28.328 | 41.735 | 48.173 |
| pgr2-off-opengl | 30.001 | 40.760 | 43.309 |
| pgr2-off-vulkan | 30.003 | 39.608 | 42.080 |
| pgr2-on-opengl | 30.004 | 40.409 | 43.188 |
| pgr2-on-vulkan | 30.006 | 39.389 | 41.282 |

FPS here is guest frame/display-write cadence, not the host presentation counter.
No Morrowind interval exceeded 75 ms. PGR2 reported no guest-frame stalls,
no focus-loss/unresponsive samples, and no lost ETW events in any run.
All eight images were inspected: Morrowind shows the resumed town, PGR2 the car
on track with its race HUD, without apparent corruption. **PGR2 images are
capture-start images; no end-of-run PGR2 image was produced by the existing
runner.** They cannot establish visual correctness for every later frame.
[All results and image phases](retail-toggle/results.csv),
[Morrowind intervals](retail-toggle/morrowind-summary.csv).

<details><summary>Inspected images by workload and renderer</summary>

- [morrowind-off-opengl (measurement-end)](retail-toggle/morrowind-off-opengl.png)
- [morrowind-off-vulkan (measurement-end)](retail-toggle/morrowind-off-vulkan.png)
- [morrowind-on-opengl (measurement-end)](retail-toggle/morrowind-on-opengl.png)
- [morrowind-on-vulkan (measurement-end)](retail-toggle/morrowind-on-vulkan.png)
- [pgr2-off-opengl (capture-start)](retail-toggle/pgr2-off-opengl.png)
- [pgr2-off-vulkan (capture-start)](retail-toggle/pgr2-off-vulkan.png)
- [pgr2-on-opengl (capture-start)](retail-toggle/pgr2-on-opengl.png)
- [pgr2-on-vulkan (capture-start)](retail-toggle/pgr2-on-vulkan.png)

</details>

The first four PGR2 attempts failed before emulator launch: copying a read-only
configuration also copied its read-only attribute, preventing the runner's
private-config update. Those failure packets remain. One retry used writable
private inputs with identical bytes/hashes; original templates, runner and
button sequence were unchanged. Morrowind was not rerun for this repair.
Retail CPU-thread/GPU-resource comparison from these traces is not yet compiled;
XISO resource measurements remain separate from these cadence measurements.

## Optional setting: same-build Vulkan comparison

Implementation `0edb216c23ab071042bb26005267d0c3816337cd`, executable SHA-256
`caeb70731620b16e85cb5fb92564a2346180edf92bdf353c8aae86c0a164d1da`, both modes.
Two full runs per mode, 149/149 records each; all live-marker checks passed.
Both on-mode runs match the off-mode regression consensus. This does not
establish hardware correctness. Native poll unit tests passed before the suite.

| Measurement | Off | On | Observation |
|---|---:|---:|---|
| Sum of leaf mean test times | 94,000.433 ms | 94,733.571 ms | +0.78% slower |
| Process CPU, host capacity | 17.486% | 13.056% | Lower CPU use |
| Process GPU utilization | 81.101% | 82.113% | Higher |
| Device GPU utilization | 35.449% | 36.454% | Higher |

Two-run observations, not a statistically established performance improvement.
[Groups](toggle-vulkan/groups.md), [full per-test rows](toggle-vulkan/per-test.csv),
[configuration and oracle audit](toggle-vulkan/audit.json),
[off](toggle-vulkan/off-vulkan-resource-usage-lanes.csv)/[on resources](toggle-vulkan/on-vulkan-resource-usage-lanes.csv).

The initial wrapper marked off-mode configuration verification failed because
it expected an explicit false key after exit. xemu saves only deviations from
defaults, so false is omitted. A TOML/default-aware verifier passed both retained
off receipts and both explicit-true on receipts. The original failure remains;
no test or sealed campaign was rerun or rewritten to correct this bookkeeping.
This configuration check does not independently prove backend availability.
OpenGL also completed 149/149 records twice per mode with required live markers
and verified off/on configuration. CPU usage fell 16.486% → 10.970% of host
capacity; leaf-test totals rose 127,419.523 → 129,058.480 ms (+1.29%). Process
GPU utilization was 81.187% → 80.533%; device GPU was 45.107% → 43.550%.
[Groups](toggle-opengl/groups.md), [per-test rows](toggle-opengl/per-test.csv),
[audit](toggle-opengl/audit.json),
[off](toggle-opengl/off-opengl-resource-usage-lanes.csv)/[on resources](toggle-opengl/on-opengl-resource-usage-lanes.csv).
OpenGL baseline output consensus still fails solely on the explicitly
inapplicable S3TC diagnostic count (14 versus 13), tracked in issue #26;
a full output-consensus pass is not claimed. Retail captures are complete; see their evidence and remaining limits above.
The default remains off.

## Historical strict Vulkan XISO comparison

Frozen S plus the shared marker harness `653e03a7f15500d3420de44af4d12cfc490558a7`
versus wait-only plus that same harness `8eeb3e48f9aedd0ca932c23802318f1b3e645fce`.
Each completed two full runs: 149/149 records, comprising 144 leaves and five
aggregates. All four receipts validate live markers without a waiver. Both
candidate runs match the two-run baseline regression consensus for 149 records;
this establishes agreement with S, not hardware correctness. Timing mode does
not enable Vulkan validation layers.

| Measurement | Baseline | Candidate | Observation |
|---|---:|---:|---|
| Sum of leaf mean test times | 93,964.545 ms | 96,072.835 ms | +2.24% slower |
| Process CPU, host capacity | 17.787% | 13.020% | Lower CPU use |
| Process GPU utilization | 81.297% | 82.398% | Higher |
| Device GPU utilization | 36.925% | 35.100% | Lower |

These are two-run descriptive measurements, not confidence intervals. The CPU
saving does not establish a suite speedup. Unfavorable individual observations
include TinyAlloc-inlineelements +27.34%, PipelineStateChurn +10.61%, and
Dxt1DirtyOnceRedraw +9.42%. See [all test rows](marker-vulkan/per-test.csv),
[group results](marker-vulkan/groups.md), [oracle comparison](marker-vulkan/oracle-comparison.json),
and [baseline](marker-vulkan/frozen-s-vulkan-resource-usage-lanes.csv)/[candidate resources](marker-vulkan/wait-only-vulkan-resource-usage-lanes.csv).
Resource samples cover process execution, not only individual marked tests.
OpenGL also completed 149/149 records twice per build. Its leaf mean total was
126,587.891 ms baseline versus 128,010.199 ms candidate (+1.12%).
[Grouped results](marker-opengl/groups.md), [all test rows](marker-opengl/per-test.csv),
and [baseline](marker-opengl/frozen-s-opengl-resource-usage-lanes.csv)/[candidate resources](marker-opengl/wait-only-opengl-resource-usage-lanes.csv).
OpenGL baseline consensus did not pass: the same unsynchronized S3TC record's
internal count differs (14 versus 15), as in the historical production pair.
The remaining normalized record fields match. This is a qualification limitation,
not a demonstrated candidate regression; it is preserved in the oracle report.

The next implementation, `0edb216c23ab071042bb26005267d0c3816337cd`, makes the wait
an optional **Tweaks → Reduce CPU usage while waiting** setting, off by default.
Its Release executable is built (SHA-256
`caeb70731620b16e85cb5fb92564a2346180edf92bdf353c8aae86c0a164d1da`), but native
qualification is pending. Earlier measurements do not qualify this new build.
The intended comparison uses this single executable with the setting off/on.

Historical uninstrumented OpenGL output comparison has a separate limitation:
the two S reports differ in the internal count (15 versus 14) for the
unsynchronized same-address S3TC test. That record already excludes framebuffer
and tile-center observations because per-draw source generation is undefined.
Source review at guest revision `baf221e339f40801fee9ddd3abf1e1a6d21a1f0a`
confirmed that the count is derived from the same excluded framebuffer readback.
The comparison tool retains that diagnostic count despite the declared
inapplicability. [Issue #26](https://github.com/Mainkill1/xemu/issues/26) records
source locations and required regression tests. A full OpenGL consensus pass
is still not claimed: the recorded tool failure remains until a separately
validated comparison repair is applied to the sealed evidence.

## Profiling-stall repair: native follow-up

The archived research head `172c4bd80a2e4583e39b83190a4a6807d8ab9115` removes
periodic profiling output while retaining explicit flush/reset controls. With
profiling enabled, four 60-second captures completed using the unchanged
snapshot/Start/B route. End images were inspected: all show the resumed town
scene, with no reconnect or pause prompt. Seed and private-HDD cleanup checks
passed. Single captures do not establish statistical non-inferiority.

| Renderer / build | Display-write cadence FPS | p95 ms | p99 ms | Intervals >150 ms |
|---|---:|---:|---:|---:|
| Vulkan busy control | 28.517 | 40.694 | 46.693 | 0 |
| Vulkan wait candidate | 28.111 | 42.016 | 48.121 | 0 |
| OpenGL busy control | 38.870 | 31.275 | 36.600 | 0 |
| OpenGL wait candidate | 38.746 | 31.624 | 36.770 | 0 |

The previous profiling-on controls had 27–28 intervals above 150 ms per minute.
The periodic pattern was absent in these follow-ups; one candidate Vulkan gap
was 76.645 ms. This supports the research profiling repair on this workload,
not qualification of the later optional-wait implementation or all freeze reports.
[Exact sources, executable hashes and measurements](profile-repair/summary.csv),
[long intervals](profile-repair/gaps.csv). The CSV's automated gameplay field
remains NOT_ESTABLISHED; the end-image inspection above is a separate observation.

## Change and integration scope

The Windows short-wait path used to repeatedly read the clock until its timeout.
The candidate waits on a persistent high-resolution timer together with the
original event handles. Necessary device timers and guest time are unchanged.

```mermaid
flowchart LR
    A[Select timeout] --> B[Arm private Windows timer]
    B --> C[Wait for timer or original handles]
    C --> D[Return only original handle readiness]
    D --> E[Dispatch pending emulator work]
```

The production extraction is `1945c40dd7d0c501949f3ab8820f0202f0fe6bd4`
on frozen S `208e4596832a1f949bd63822d5ddd962c4575067`. It excludes
research PTIMER, QPC and attribution changes. Exact executable identities are
in [build-identities.json](build-identities.json). Marker-validation identities
are separate: both sides add the same existing opt-in XISO harness. They must
not be described as the uninstrumented production heads.

Builds use matching Windows Release settings, full LTO, x86-64-v3, and toolchain
`ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2`.

## Morrowind: frozen S versus wait-only

One 60-second capture per build/renderer. The fixed snapshot was restored;
QMP running, wait 5 seconds, Start held 150 ms, wait 2 seconds, B held 150 ms,
wait 2 seconds, then uninterrupted measurement. No resource sampler in these
cells. Each used private writable state and verified immutable-seed cleanup.
Reviewed end images showed the intended scene without the reconnect/pause menu.
A nonblank image alone is not a gameplay oracle.

| Renderer | Build | Cadence FPS | p95 ms | p99 ms | Maximum ms | Gaps >75 ms |
|---|---|---:|---:|---:|---:|---:|
| Vulkan | S | 28.424 | 41.385 | 47.826 | 78.443 | 1 |
| Vulkan | Wait-only | 27.819 | 43.114 | 48.615 | 75.277 | 1 |
| OpenGL | S | 37.301 | 32.845 | 38.024 | 46.356 | 0 |
| OpenGL | Wait-only | 38.441 | 31.480 | 37.009 | 45.844 | 0 |

Cadence is NV2A display-write activity, **not displayed FPS**. Vulkan cadence was
2.13% lower and OpenGL 3.06% higher in these single pairs. Neither is a
statistically established performance change. No interval exceeded 150 ms.

[Full measurements](morrowind-s-wait.csv),
[long intervals](morrowind-s-wait-gaps.csv).

## Morrowind: research-profiling negative control

All four captures used the identical research executable from
`8cabc23d9edda3b2a12a6d64983bcec076237fdc`, identical Vulkan configuration,
snapshot and Start/B sequence, and no resource sampler. The only selected
change was `XEMU_QEMU_POLL_PROFILE=1` versus `0`.

| Order | Profiling | Cadence FPS | p95 ms | p99 ms | Gaps >75 ms | Gaps >150 ms |
|---:|---|---:|---:|---:|---:|---:|
| 1 | On | 24.599 | 47.773 | 171.228 | 29 | 27 |
| 2 | Off | 28.303 | 42.041 | 47.522 | 0 | 0 |
| 3 | Off | 28.448 | 42.468 | 47.682 | 1 | 0 |
| 4 | On | 25.984 | 44.408 | 174.167 | 29 | 28 |

Profiling repeatedly triggered the approximately 2.1-second stall pattern in
this configuration. The recording paths synchronously emitted diagnostic
batches during emulation. Research repair
`172c4bd80a2e4583e39b83190a4a6807d8ab9115` removes automatic emission and
retains explicit reset/flush controls. A compiled source test failed before
that repair and passes after it under ASan/UBSan; native repair validation is
still pending. This does not establish the cause of every earlier freeze.

[Full measurements](morrowind-profile-on-off.csv),
[every long interval](morrowind-profile-on-off-gaps.csv).

## XISO production: diagnostic comparison

Both sides passed all 149 records in two full passes on each renderer. No records were dropped.
The 144 leaf records contribute to time sums; five aggregate records contribute
to correctness counts but are excluded from sums to prevent double counting.

Vulkan results:

| Measurement | S | Wait-only | Observation |
|---|---:|---:|---|
| Sum of mean leaf test times | 100.642 s | 99.784 s | 0.85% lower |
| Mean host wall time | 140.252 s | 133.733 s | Includes runner/startup overhead |
| Mean host CPU capacity used | 17.63% | 12.66% | Lower CPU use |
| Mean process GPU engine sum | 72.59% | 73.42% | Higher |
| Mean device GPU utilization | 32.00% | 34.76% | Higher |
| Mean process private bytes | 3,025,643,985 | 3,045,234,981 | Higher |

OpenGL results:

| Measurement | S | Wait-only | Observation |
|---|---:|---:|---|
| Sum of mean leaf test times | 127.578 s | 129.123 s | 1.21% higher |
| Mean host wall time | 175.762 s | 173.243 s | Includes runner/startup overhead |
| Mean host CPU capacity used | 16.47% | 10.96% | Lower CPU use |
| Mean process GPU engine sum | 80.87% | 80.98% | Higher |
| Mean device GPU utilization | 44.61% | 43.82% | Lower |
| Mean process private bytes | 2,286,598,963 | 2,287,487,953 | Slightly higher |

These production binaries lack live timing boundaries. The measurements are
**diagnostic, not a qualified speedup**. Resource samples cover the launch/run,
not a precisely isolated guest measurement phase. The process GPU engine sum
and device utilization measure different things; neither is GPU power.

Unfavorable leaf observations include Dxt1DirtyOnceRedraw +12.09%,
Pgr2AiBackup StreamingOnly +11.55%, Dxt1RingPayloadGenerations +11.00%,
SurfaceListLookup002 +8.28%, and UniformThrash +7.73%. Signed changes describe
these runs; no sign-only improvement/regression classification is applied.

[Per-test comparison](xiso-vulkan-production-per-test.csv),
[baseline full timing rows](xiso-vulkan-production-baseline.csv),
[candidate full timing rows](xiso-vulkan-production-candidate.csv),
[baseline resources](xiso-vulkan-production-baseline-resources.csv),
[candidate resources](xiso-vulkan-production-candidate-resources.csv).

## Test suite groups

All rows below are diagnostic correctness-mode timings. Full leaf CSVs retain
each measured test; aggregate records are not double-counted in time totals.

<details>
<summary>Vulkan groups</summary>

| XISO group | Records B/C pass | Leaf records | Baseline leaf total (ms) | Candidate leaf total (ms) | Observed change |
|---|---:|---:|---:|---:|---:|
| BusyPfifo | 2/2; 2/2 | 2 | 5674.524 | 5531.187 | -2.53% |
| CpuFloatingPoint | 2/2; 2/2 | 2 | 12388.019 | 12419.027 | +0.25% |
| CpuTranslationBlocks | 3/3; 3/3 | 3 | 5338.291 | 5208.588 | -2.43% |
| FillRate | 2/2; 2/2 | 2 | 38.488 | 37.246 | -3.23% |
| GameLoadComposite | 55/55; 55/55 | 52 | 57588.194 | 57271.450 | -0.55% |
| High vertex count | 4/4; 4/4 | 4 | 1521.659 | 1462.883 | -3.86% |
| PFIFOArrayElements | 3/3; 3/3 | 3 | 22.520 | 22.280 | -1.07% |
| PipelineTextureSwitch | 4/4; 4/4 | 4 | 684.713 | 687.595 | +0.42% |
| PrimitiveType | 20/20; 20/20 | 20 | 171.927 | 167.753 | -2.43% |
| ReportQuery | 8/8; 8/8 | 8 | 77.714 | 75.761 | -2.51% |
| SurfaceRendering | 28/28; 28/28 | 26 | 5897.260 | 5932.146 | +0.59% |
| TinyDraw | 8/8; 8/8 | 8 | 1978.951 | 1994.270 | +0.77% |
| UniformThrash | 1/1; 1/1 | 1 | 27.488 | 29.612 | +7.73% |
| Vertex buffer allocation | 9/9; 9/9 | 9 | 9232.476 | 8944.146 | -3.12% |

</details>

<details>
<summary>Opengl groups</summary>

| XISO group | Records B/C pass | Leaf records | Baseline leaf total (ms) | Candidate leaf total (ms) | Observed change |
|---|---:|---:|---:|---:|---:|
| BusyPfifo | 2/2; 2/2 | 2 | 5251.213 | 5292.408 | +0.78% |
| CpuFloatingPoint | 2/2; 2/2 | 2 | 12496.594 | 12571.088 | +0.60% |
| CpuTranslationBlocks | 3/3; 3/3 | 3 | 5276.760 | 5237.404 | -0.75% |
| FillRate | 2/2; 2/2 | 2 | 29.662 | 32.844 | +10.73% |
| GameLoadComposite | 55/55; 55/55 | 52 | 87575.230 | 88649.539 | +1.23% |
| High vertex count | 4/4; 4/4 | 4 | 1393.168 | 1382.920 | -0.74% |
| PFIFOArrayElements | 3/3; 3/3 | 3 | 35.115 | 31.299 | -10.87% |
| PipelineTextureSwitch | 4/4; 4/4 | 4 | 492.555 | 489.668 | -0.59% |
| PrimitiveType | 20/20; 20/20 | 20 | 183.837 | 187.494 | +1.99% |
| ReportQuery | 8/8; 8/8 | 8 | 16.797 | 17.059 | +1.56% |
| SurfaceRendering | 28/28; 28/28 | 26 | 2380.736 | 2462.485 | +3.43% |
| TinyDraw | 8/8; 8/8 | 8 | 3611.197 | 3748.657 | +3.81% |
| UniformThrash | 1/1; 1/1 | 1 | 16.316 | 18.700 | +14.61% |
| Vertex buffer allocation | 9/9; 9/9 | 9 | 8818.447 | 9001.552 | +2.08% |

</details>

OpenGL [per-test comparison](xiso-opengl-production-per-test.csv),
[baseline rows](xiso-opengl-production-baseline.csv),
[candidate rows](xiso-opengl-production-candidate.csv),
[baseline resources](xiso-opengl-production-baseline-resources.csv), and
[candidate resources](xiso-opengl-production-candidate-resources.csv).

OpenGL unfavorable group observations include UniformThrash +14.61%,
FillRate +10.73%, TinyDraw +3.81%, and SurfaceRendering +3.43%.
Host wall time and guest measured time have different boundaries and even
move in opposite directions here; do not substitute one for the other.

## Exclusions and remaining gates

Two PGR2 captures were falsely marked complete while still in menus:
the research control/OpenGL at Select Transmission (56.981 FPS), and a later
research candidate/OpenGL at Start Race (58.907 FPS). Both are excluded from
race comparisons. Menu animation and successful key injection are not proof
that the target gameplay state was reached. The original PFIFOSaturation stall
also remains unexplained despite successful later reproduction attempts.

Required before acceptance: finish both-renderer XISO comparisons with valid
marker timing, inspect unfavorable per-test results, qualify the repaired
profiling head natively, verify full-start PGR2 gameplay, and complete repeated
production CPU/frame-time comparisons. The previous large CPU savings from a
different research head do not qualify this extraction or prove faster XISO
completion. Keep the lane held until these requirements are satisfied.
