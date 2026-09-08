# PR #18 exact-head qualification: linear texture source spans

## Result

**PASS for correctness and runtime qualification at source `2e801ab07c4a8d0a87defd2ba70ddad3b3e1041c`.**

The old head approved a six-byte 3x1 YUY2 row, then the real converter read byte offset 7 at a protected page boundary. The repaired head rounds packed 4:2:2 source widths to complete two-pixel macropixels and uses that same checked span in both public length paths.

## Exact identity

- Parent source: `37823ccf18263764c82ceb6698201330a22a6c65`
- Candidate source: `2e801ab07c4a8d0a87defd2ba70ddad3b3e1041c`
- Candidate source tree: `33db580927ed80d512ddf5c68a7ba41aaecb2d66`
- Windows Release SHA-256: `f4503e602d89aec281f187eba5c52eca446d781e839abac750c6631df62d5419`
- Native Windows texture-unit executable SHA-256: `2f3ec10a4330785714414d9e9e33d9e84fbc51f819810c91d04f9c57e17d591d`

## Correctness proof

| Check | Result |
|---|---|
| Old-head guarded 3x1 YUY2 negative control | ASan fault at actual converter source offset 7 |
| Repaired guarded YUY2/UYVY exact and padded cases | 16/16 PASS under ASan/UBSan |
| Complete focused source unit suite | 15/15 PASS under ASan/UBSan |
| Exact native Windows unit suite | 15/15 PASS |
| Expanded candidate XISO | 4/4 cells, 152/152 records per cell |
| Matched parent/candidate XISO | 4/4 cells, 147/147 records per cell |
| Vulkan validation | zero VUIDs in all Vulkan cells |
| PGR2 | 8/8 ABBA cells PASS |
| Morrowind | 8/8 cells across two matched runs PASS |
| Process/private-HDD cleanup | PASS; no quarantine |

## Performance summary

Higher cadence and lower p95/p99 are favorable. PGR2 values are means of two 60-second ABBA runs per build/renderer. Morrowind values are means of two matched 10-second snapshot runs per build/renderer.

| Workload / metric | Renderer | Baseline | Candidate | Delta |
|---|---|---:|---:|---:|
| PGR2 cadence | VULKAN | 30.004 FPS | 30.006 FPS | +0.01% |
| PGR2 p95 | VULKAN | 39.526 ms | 39.404 ms | -0.31% |
| PGR2 p99 | VULKAN | 41.678 ms | 41.734 ms | +0.13% |
| PGR2 cadence | OPENGL | 30.008 FPS | 29.996 FPS | -0.04% |
| PGR2 p95 | OPENGL | 39.894 ms | 40.341 ms | +1.12% |
| PGR2 p99 | OPENGL | 42.572 ms | 42.978 ms | +0.95% |
| Morrowind cadence proxy | VULKAN | 27.445 FPS | 27.575 FPS | +0.47% |
| Morrowind p95 | VULKAN | 44.169 ms | 43.820 ms | -0.79% |
| Morrowind p99 | VULKAN | 48.946 ms | 48.844 ms | -0.21% |
| Morrowind cadence proxy | OPENGL | 38.139 FPS | 37.216 FPS | -2.42% |
| Morrowind p95 | OPENGL | 32.246 ms | 32.539 ms | +0.91% |
| Morrowind p99 | OPENGL | 38.204 ms | 38.641 ms | +1.14% |

Morrowind changed direction between the first and second pair on both renderers. The two-pair mean is neutral on Vulkan. OpenGL cadence is 2.42% lower while p95/p99 remain within 1.2%; this is retained as a visible performance flag rather than attributed to the patch from two short, directionally inconsistent pairs.

## Expanded XISO test groups

| Test group | Renderer | Tests | Passed checks | Result |
|---|---|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 4/4 | PASS |
| CpuFloatingPoint | OPENGL | 2 | 4/4 | PASS |
| CpuTranslationBlocks | OPENGL | 3 | 6/6 | PASS |
| FillRate | OPENGL | 2 | 4/4 | PASS |
| GameLoadComposite | OPENGL | 55 | 110/110 | PASS |
| High vertex count | OPENGL | 4 | 8/8 | PASS |
| PFIFOArrayElements | OPENGL | 3 | 6/6 | PASS |
| PipelineTextureSwitch | OPENGL | 4 | 8/8 | PASS |
| PrimitiveType | OPENGL | 20 | 40/40 | PASS |
| ReportQuery | OPENGL | 8 | 16/16 | PASS |
| SurfaceRendering | OPENGL | 28 | 56/56 | PASS |
| TextureCubemapFallback | OPENGL | 3 | 6/6 | PASS |
| TinyDraw | OPENGL | 8 | 16/16 | PASS |
| UniformThrash | OPENGL | 1 | 2/2 | PASS |
| Vertex buffer allocation | OPENGL | 9 | 18/18 | PASS |
| BusyPfifo | VULKAN | 2 | 4/4 | PASS |
| CpuFloatingPoint | VULKAN | 2 | 4/4 | PASS |
| CpuTranslationBlocks | VULKAN | 3 | 6/6 | PASS |
| FillRate | VULKAN | 2 | 4/4 | PASS |
| GameLoadComposite | VULKAN | 55 | 110/110 | PASS |
| High vertex count | VULKAN | 4 | 8/8 | PASS |
| PFIFOArrayElements | VULKAN | 3 | 6/6 | PASS |
| PipelineTextureSwitch | VULKAN | 4 | 8/8 | PASS |
| PrimitiveType | VULKAN | 20 | 40/40 | PASS |
| ReportQuery | VULKAN | 8 | 16/16 | PASS |
| SurfaceRendering | VULKAN | 28 | 56/56 | PASS |
| TextureCubemapFallback | VULKAN | 3 | 6/6 | PASS |
| TinyDraw | VULKAN | 8 | 16/16 | PASS |
| UniformThrash | VULKAN | 1 | 2/2 | PASS |
| Vertex buffer allocation | VULKAN | 9 | 18/18 | PASS |

## Matched XISO timing context

The matched suite has no live guest timing markers. A flag requires average and median group sums to move at least 5% in the same direction. Timing direction is context, not a causal performance claim.

| Test group | Renderer | Tests | Baseline average sum (us) | Candidate average sum (us) | Delta | Result |
|---|---|---:|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 4,547,206 | 4,433,463 | -2.50% | NEUTRAL_CONTEXT |
| CpuFloatingPoint | OPENGL | 2 | 1,266,018 | 1,240,048 | -2.05% | NEUTRAL_CONTEXT |
| CpuTranslationBlocks | OPENGL | 3 | 516,641 | 515,025 | -0.31% | NEUTRAL_CONTEXT |
| FillRate | OPENGL | 2 | 3,127 | 2,809 | -10.17% | IMPROVE_FLAG |
| GameLoadComposite | OPENGL | 55 | 10,599,337 | 10,765,517 | +1.57% | NEUTRAL_CONTEXT |
| High vertex count | OPENGL | 4 | 208,108 | 215,521 | +3.56% | NEUTRAL_CONTEXT |
| PFIFOArrayElements | OPENGL | 3 | 3,755 | 3,888 | +3.54% | NEUTRAL_CONTEXT |
| PipelineTextureSwitch | OPENGL | 4 | 57,004 | 57,146 | +0.25% | NEUTRAL_CONTEXT |
| PrimitiveType | OPENGL | 20 | 17,711 | 18,134 | +2.39% | NEUTRAL_CONTEXT |
| ReportQuery | OPENGL | 6 | 3,483 | 3,988 | +14.50% | REGRESSION_FLAG |
| SurfaceRendering | OPENGL | 28 | 347,001 | 346,319 | -0.20% | NEUTRAL_CONTEXT |
| TinyDraw | OPENGL | 8 | 334,331 | 338,900 | +1.37% | NEUTRAL_CONTEXT |
| UniformThrash | OPENGL | 1 | 1,741 | 1,779 | +2.18% | NEUTRAL_CONTEXT |
| Vertex buffer allocation | OPENGL | 9 | 855,784 | 865,512 | +1.14% | NEUTRAL_CONTEXT |
| BusyPfifo | VULKAN | 2 | 4,921,607 | 4,906,349 | -0.31% | NEUTRAL_CONTEXT |
| CpuFloatingPoint | VULKAN | 2 | 1,251,309 | 1,249,642 | -0.13% | NEUTRAL_CONTEXT |
| CpuTranslationBlocks | VULKAN | 3 | 521,017 | 519,701 | -0.25% | NEUTRAL_CONTEXT |
| FillRate | VULKAN | 2 | 3,609 | 3,610 | +0.03% | NEUTRAL_CONTEXT |
| GameLoadComposite | VULKAN | 55 | 7,397,375 | 7,435,141 | +0.51% | NEUTRAL_CONTEXT |
| High vertex count | VULKAN | 4 | 226,642 | 229,522 | +1.27% | NEUTRAL_CONTEXT |
| PFIFOArrayElements | VULKAN | 3 | 2,767 | 2,784 | +0.61% | NEUTRAL_CONTEXT |
| PipelineTextureSwitch | VULKAN | 4 | 82,064 | 82,228 | +0.20% | NEUTRAL_CONTEXT |
| PrimitiveType | VULKAN | 20 | 16,948 | 16,724 | -1.32% | NEUTRAL_CONTEXT |
| ReportQuery | VULKAN | 6 | 17,188 | 17,003 | -1.08% | NEUTRAL_CONTEXT |
| SurfaceRendering | VULKAN | 28 | 1,043,879 | 1,036,619 | -0.70% | NEUTRAL_CONTEXT |
| TinyDraw | VULKAN | 8 | 178,593 | 181,355 | +1.55% | NEUTRAL_CONTEXT |
| UniformThrash | VULKAN | 1 | 2,811 | 2,800 | -0.39% | NEUTRAL_CONTEXT |
| Vertex buffer allocation | VULKAN | 9 | 887,588 | 897,410 | +1.11% | NEUTRAL_CONTEXT |

## PGR2 resource context

| Metric | Renderer | Baseline | Candidate | Delta |
|---|---|---:|---:|---:|
| Mean host CPU | VULKAN | 21.578% | 22.070% | +2.28% |
| Mean process GPU engine | VULKAN | 45.073% | 44.076% | -2.21% |
| Mean device GPU | VULKAN | 35.800% | 34.150% | -4.61% |
| Private memory | VULKAN | 3106.201 MiB | 3110.444 MiB | +0.14% |
| Dedicated GPU memory | VULKAN | 674.309 MiB | 674.352 MiB | +0.01% |
| Shared GPU memory | VULKAN | 467.709 MiB | 468.401 MiB | +0.15% |
| NVIDIA board power | VULKAN | 21.416 W | 21.405 W | -0.05% |
| Mean host CPU | OPENGL | 22.356% | 22.517% | +0.72% |
| Mean process GPU engine | OPENGL | 44.454% | 44.534% | +0.18% |
| Mean device GPU | OPENGL | 38.650% | 38.250% | -1.03% |
| Private memory | OPENGL | 2284.743 MiB | 2282.514 MiB | -0.10% |
| Dedicated GPU memory | OPENGL | 160.869 MiB | 160.612 MiB | -0.16% |
| Shared GPU memory | OPENGL | 41.625 MiB | 41.875 MiB | +0.60% |
| NVIDIA board power | OPENGL | 23.959 W | 23.922 W | -0.16% |

Power follows completed host/GPU work and operating state. It is reported with cadence and utilization and is not interpreted alone as an improvement or regression.

## Files

- `xiso-matched-per-test.csv`: every matched XISO leaf
- `xiso-group-summary.csv`: grouped matched timing context
- `xiso-expanded-group-summary.csv`: exact-head expanded-suite groups
- `pgr2-runs-and-resources.csv`: all eight retail runs and resource samples
- `pgr2-summary.csv` and `morrowind-summary.csv`: calculated comparisons
- raw normalized XISO JSON and terminal receipts
- guarded negative/positive sanitizer logs and build/unit receipts
