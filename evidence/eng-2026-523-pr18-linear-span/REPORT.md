# GitHub PR 18 qualification

Status: **PASS**

- Parent: `37823ccf18263764c82ceb6698201330a22a6c65`
- Candidate: `12022674a6e5119965005d3c58d373fcbcec93fe`

## Test suite summary

| Test group | Leaf tests |
|---|---:|
| busy_pfifo | 2 |
| cpu_floating_point | 2 |
| cpu_translation_blocks | 3 |
| fill_rate | 2 |
| game_load | 52 |
| high_vertex_count | 4 |
| pfifo_array_elements | 3 |
| pipeline_texture_switch | 4 |
| primitive_type | 20 |
| report_query | 8 |
| surface | 26 |
| texture_cubemap_fallback | 3 |
| tiny_draw | 8 |
| uniform_thrash | 1 |
| vertex_buffer_allocation | 9 |
| **Total** | **147 leaves + 5 group records = 152 records** |

- Full correctness gate: 4/4 cells PASS, 152 records per cell.
- Matched parent/candidate comparison: 4/4 cells PASS, 147 records per cell.
- Vulkan validation: zero VUIDs in every Vulkan cell.
- Process cleanup: complete; host was not quarantined.

## Matched XISO group timing

The 147-record parent/candidate comparison ran once per renderer. Group sums use each leaf record’s average and median time. A context flag requires both sums to move at least 5% in the same direction; these markerless microbenchmarks do not determine acceptance.

| Test group | Renderer | Tests | Baseline average sum | Candidate average sum | Average delta | Baseline median sum | Candidate median sum | Median delta | Result |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 4,646,918 us | 4,666,413 us | +0.42% | 4,646,954 us | 4,667,159 us | +0.43% | Neutral/context |
| BusyPfifo | VULKAN | 2 | 4,884,505 us | 4,932,566 us | +0.98% | 4,883,961 us | 4,932,374 us | +0.99% | Neutral/context |
| CpuFloatingPoint | OPENGL | 2 | 1,258,310 us | 1,240,720 us | -1.40% | 1,257,971 us | 1,241,779 us | -1.29% | Neutral/context |
| CpuFloatingPoint | VULKAN | 2 | 1,246,201 us | 1,246,047 us | -0.01% | 1,244,393 us | 1,242,076 us | -0.19% | Neutral/context |
| CpuTranslationBlocks | OPENGL | 3 | 518,606 us | 519,179 us | +0.11% | 517,506 us | 517,368 us | -0.03% | Neutral/context |
| CpuTranslationBlocks | VULKAN | 3 | 519,477 us | 525,357 us | +1.13% | 519,386 us | 525,470 us | +1.17% | Neutral/context |
| FillRate | OPENGL | 2 | 3,244 us | 2,790 us | -14.00% | 2,761 us | 2,508 us | -9.16% | Improve flag |
| FillRate | VULKAN | 2 | 3,788 us | 3,552 us | -6.23% | 2,031 us | 1,929 us | -5.02% | Improve flag |
| GameLoadComposite | OPENGL | 52 | 10,788,823 us | 10,703,190 us | -0.79% | 10,777,302 us | 10,704,364 us | -0.68% | Neutral/context |
| GameLoadComposite | VULKAN | 52 | 7,324,029 us | 7,318,564 us | -0.07% | 7,306,795 us | 7,318,697 us | +0.16% | Neutral/context |
| High vertex count | OPENGL | 4 | 205,923 us | 206,938 us | +0.49% | 205,011 us | 206,758 us | +0.85% | Neutral/context |
| High vertex count | VULKAN | 4 | 224,615 us | 223,177 us | -0.64% | 222,663 us | 221,667 us | -0.45% | Neutral/context |
| PFIFOArrayElements | OPENGL | 3 | 3,974 us | 3,688 us | -7.20% | 3,833 us | 3,813 us | -0.52% | Neutral/context |
| PFIFOArrayElements | VULKAN | 3 | 2,789 us | 2,820 us | +1.11% | 1,934 us | 1,949 us | +0.78% | Neutral/context |
| PipelineTextureSwitch | OPENGL | 4 | 56,039 us | 56,178 us | +0.25% | 55,651 us | 55,670 us | +0.03% | Neutral/context |
| PipelineTextureSwitch | VULKAN | 4 | 82,239 us | 84,821 us | +3.14% | 80,427 us | 82,495 us | +2.57% | Neutral/context |
| PrimitiveType | OPENGL | 20 | 18,224 us | 17,522 us | -3.85% | 16,991 us | 16,471 us | -3.06% | Neutral/context |
| PrimitiveType | VULKAN | 20 | 16,983 us | 16,687 us | -1.74% | 12,694 us | 12,589 us | -0.83% | Neutral/context |
| ReportQuery | OPENGL | 6 | 3,628 us | 3,425 us | -5.60% | 3,458 us | 3,420 us | -1.10% | Neutral/context |
| ReportQuery | VULKAN | 6 | 16,587 us | 16,882 us | +1.78% | 15,840 us | 16,066 us | +1.43% | Neutral/context |
| SurfaceRendering | OPENGL | 26 | 343,579 us | 342,729 us | -0.25% | 326,385 us | 326,990 us | +0.19% | Neutral/context |
| SurfaceRendering | VULKAN | 26 | 1,056,803 us | 995,857 us | -5.77% | 1,014,650 us | 953,746 us | -6.00% | Improve flag |
| TinyDraw | OPENGL | 8 | 340,195 us | 330,142 us | -2.96% | 340,316 us | 329,293 us | -3.24% | Neutral/context |
| TinyDraw | VULKAN | 8 | 179,021 us | 176,943 us | -1.16% | 178,188 us | 176,436 us | -0.98% | Neutral/context |
| UniformThrash | OPENGL | 1 | 1,701 us | 1,772 us | +4.17% | 1,695 us | 1,695 us | +0.00% | Neutral/context |
| UniformThrash | VULKAN | 1 | 2,872 us | 2,740 us | -4.60% | 2,050 us | 2,036 us | -0.68% | Neutral/context |
| Vertex buffer allocation | OPENGL | 9 | 845,211 us | 841,314 us | -0.46% | 847,925 us | 843,262 us | -0.55% | Neutral/context |
| Vertex buffer allocation | VULKAN | 9 | 881,252 us | 878,273 us | -0.34% | 887,191 us | 882,600 us | -0.52% | Neutral/context |

Context improvement flags: FillRate OPENGL (-14.00% avg, -9.16% median), FillRate VULKAN (-6.23% avg, -5.02% median), SurfaceRendering VULKAN (-5.77% avg, -6.00% median)

Context regression flags: None.

Full group rows are in `xiso-group-summary.csv`.

## Performance summary

Values are arithmetic means of the sealed runs. Morrowind has one run per cell; PGR2 has two runs per build/renderer in ABBA order. Signed changes are descriptive and do not establish a regression or improvement by themselves.

| Game | Renderer | Build | n | Avg FPS | p95 ms | p99 ms | Host FPS |
|---|---|---|---:|---:|---:|---:|---:|
| Morrowind | VULKAN | baseline | 1 | 27.350 | 41.792 | 48.693 |  |
| Morrowind | VULKAN | candidate | 1 | 27.423 | 45.151 | 50.609 |  |
| Morrowind | OPENGL | baseline | 1 | 38.235 | 32.599 | 37.638 |  |
| Morrowind | OPENGL | candidate | 1 | 37.587 | 33.977 | 38.010 |  |
| PGR2 | VULKAN | baseline | 2 | 30.011 | 38.904 | 41.369 | 60.004 |
| PGR2 | VULKAN | candidate | 2 | 29.997 | 40.130 | 42.024 | 59.998 |
| PGR2 | OPENGL | baseline | 2 | 29.996 | 40.892 | 43.666 | 60.000 |
| PGR2 | OPENGL | candidate | 2 | 30.009 | 41.038 | 44.171 | 59.998 |

## Observed candidate deltas

| Game | Renderer | Avg FPS | p95 | p99 | Host FPS |
|---|---|---:|---:|---:|---:|
| Morrowind | VULKAN | 0.27% | 8.04% | 3.93% |  |
| Morrowind | OPENGL | -1.69% | 4.23% | 0.99% |  |
| PGR2 | VULKAN | -0.05% | 3.15% | 1.58% | -0.01% |
| PGR2 | OPENGL | 0.04% | 0.36% | 1.16% | -0.00% |

Lower p95/p99 is favorable. Higher FPS is favorable. The single-run Morrowind tail changes require repetition before a performance verdict.

## PGR2 resource summary

Two runs per build/renderer. GPU values use process engine counters and NVIDIA device samples from the same measurement windows.

| Renderer | Build | CPU host % | Process GPU % | Device GPU % | Private MiB | Dedicated GPU MiB | Shared GPU MiB | Power W |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| VULKAN | baseline | 21.137 | 43.774 | 35.300 | 3119.127 | 675.566 | 467.712 | 21.283 |
| VULKAN | candidate | 21.694 | 42.918 | 35.100 | 3118.529 | 675.567 | 468.375 | 21.002 |
| OPENGL | baseline | 22.833 | 44.559 | 38.100 | 2283.892 | 162.238 | 41.625 | 23.788 |
| OPENGL | candidate | 22.843 | 44.264 | 38.450 | 2288.164 | 161.609 | 41.625 | 23.828 |

## Qualification limit

The exact Release build, 13 unit cases, full XISO checks, Morrowind, and PGR2 are covered. A guarded decoder/preparation test at the exact DMA/VRAM boundary is still missing, so PR #18 remains draft.

Full rows are in `xiso-per-test.csv`, `xiso-results.csv`, `xiso-group-summary.csv`, and `retail-results.csv`.
