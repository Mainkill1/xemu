# Windows short-wait investigation

Status: **held; qualification incomplete**. This records completed diagnostics,
not an accepted XISO speedup or a complete PGR2 lag fix. Related work:
[issue 19](https://github.com/Mainkill1/xemu/issues/19) and
[PR 24](https://github.com/Mainkill1/xemu/pull/24).

## Latest strict Vulkan XISO comparison

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
OpenGL comparison remains pending collection.

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
The count's semantics still need checking; a full OpenGL consensus pass is not
claimed, and this difference is not attributed to the wait candidate.

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
