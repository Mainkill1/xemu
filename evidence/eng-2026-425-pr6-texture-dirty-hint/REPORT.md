# GitHub PR 6 qualification

Status: **PASS_WITH_KNOWN_PARENT_FAILURE**

- Baseline: `88fc5c9bef5663f075fb514a7ceef904be3a49e8`
- Candidate: `c1462ad7d88a1d9d20c42ee70ed3661276039aba`
- Baseline executable SHA-256: `b3e0050e857c97bc8ef79d3248a6659125ddbddfc0f8a972436d11411e47ea78`
- Candidate executable SHA-256: `818614db65e5f6de83a93527f69468e106911b949b825e8177b38c2676e3d943`
- Expanded XISO/catalog SHA-256: `1426632c6c37005a724184329ed0de4a3cf7dc75a9102c0e9ba255380a93a31f` / `a0007a04191d6900b861e227f9110ba856857e68847bb683452b50ec7d2b50f2`
- Matched XISO/catalog SHA-256: `08551d0c0b7bc5efb20a7b36d6d4f0e24ab666b4cee25930232858ccd9872a3e` / `027065948624d6aafdbe557bed8123eb6dcaa83353cf242c71d030a109b64578`

## Test suite summary

| Suite | OpenGL | Vulkan |
|---|---|---|
| Expanded 153-record suite | PASS_WITH_KNOWN_PARENT_FAILURE | Known parent failure: PR 15 bordered-mip copy |
| Matched 147-record suite | PASS | PASS |

- Morrowind: PASS, 4/4 cells.
- PGR2: PASS, 8/8 ABBA cells.
- Cleanup: PASS; host not quarantined.
- Production-path dirty-hint oracle: PASS.

## Production-path dirty-hint oracle

The same compile-time test probe was applied to pre-repair `88fc5c9b` and repaired `c1462ad7`. It records the real `check_bound_texture_memory_dirty()` to `pgraph_vk_bind_textures()` to `create_texture()` route and is absent from ordinary Release builds. Both instrumented Vulkan runs passed all 147 XISO records. Probe-build timing is ineligible because each event is flushed to disk.

| Production event | Pre-repair | Repaired | Change |
|---|---:|---:|---:|
| Texture hash | 439,138 | 19,680 | -95.52% |
| Completed revalidation | 438,850 | 19,392 | -95.58% |
| Bound-dirty gate | 348,785 | 1,354 | -99.61% |
| Fast-path bind | 205,943 | 553,374 | +168.70% |
| Successful completion retaining dirty hint | 438,850 | 0 | eliminated |
| Successful changed-payload completion | 13,512 | 13,458 | -0.40% |
| Texture start pages observed with multiple bindings | 33 | 33 | preserved |

The DXT1 and RGBA8 dirty-once records each passed 128 no-write redraws per build with identical work/result/tile-center KATs. Changed-payload completion stayed effectively constant while redundant hashes fell by 95.52%, proving that the repair retires handled hints rather than skipping legitimate uploads. The focused 5/5 unit executable independently covers unchanged payload, changed payload, upload failure/retry state, palette-only hash change, and two bindings sharing a page.

Compact evidence: `production-path-summary.json`, `production-path-xiso-records.csv`, `production-path-unit.txt`, both probe diffs, and `verify-texture-revalidation-probe.py`. Raw event streams remain sealed by SHA-256 in the structured summary; they are 134.6 MB and 40.4 MB and are not added to Git history.

### Expanded 153-record groups

| Test group | Renderer | Tests | Runs | Passed checks | Result |
|---|---|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 1 | 2/2 | PASS |
| BusyPfifo | VULKAN | 2 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| CpuFloatingPoint | OPENGL | 2 | 1 | 2/2 | PASS |
| CpuFloatingPoint | VULKAN | 2 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| CpuTranslationBlocks | OPENGL | 3 | 1 | 3/3 | PASS |
| CpuTranslationBlocks | VULKAN | 3 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| FillRate | OPENGL | 2 | 1 | 2/2 | PASS |
| FillRate | VULKAN | 2 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| GameLoadComposite | OPENGL | 52 | 1 | 52/52 | PASS |
| GameLoadComposite | VULKAN | 52 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| High vertex count | OPENGL | 4 | 1 | 4/4 | PASS |
| High vertex count | VULKAN | 4 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| PFIFOArrayElements | OPENGL | 3 | 1 | 3/3 | PASS |
| PFIFOArrayElements | VULKAN | 3 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| PipelineTextureSwitch | OPENGL | 4 | 1 | 4/4 | PASS |
| PipelineTextureSwitch | VULKAN | 4 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| PrimitiveType | OPENGL | 20 | 1 | 20/20 | PASS |
| PrimitiveType | VULKAN | 20 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| ReportQuery | OPENGL | 8 | 1 | 8/8 | PASS |
| ReportQuery | VULKAN | 8 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| SurfaceRendering | OPENGL | 26 | 1 | 26/26 | PASS |
| SurfaceRendering | VULKAN | 26 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| TextureCubemapFallback | OPENGL | 4 | 1 | 0/4 | KNOWN_PARENT_FAILURE |
| TextureCubemapFallback | VULKAN | 4 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| TinyDraw | OPENGL | 8 | 1 | 8/8 | PASS |
| TinyDraw | VULKAN | 8 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| UniformThrash | OPENGL | 1 | 1 | 1/1 | PASS |
| UniformThrash | VULKAN | 1 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| Vertex buffer allocation | OPENGL | 9 | 1 | 9/9 | PASS |
| Vertex buffer allocation | VULKAN | 9 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |

## Performance summary

| Game | Renderer | Build | n | Avg FPS | p95 ms | p99 ms | Host FPS |
|---|---|---|---:|---:|---:|---:|---:|
| Morrowind | OPENGL | baseline | 1 | 37.383 | 33.568 | 37.603 |  |
| Morrowind | OPENGL | candidate | 1 | 37.560 | 33.642 | 39.285 |  |
| Morrowind | VULKAN | baseline | 1 | 18.851 | 64.803 | 67.317 |  |
| Morrowind | VULKAN | candidate | 1 | 18.668 | 62.152 | 67.822 |  |
| PGR2 | OPENGL | baseline | 2 | 24.870 | 54.630 | 66.704 | 59.998 |
| PGR2 | OPENGL | candidate | 2 | 24.589 | 55.422 | 66.471 | 59.999 |
| PGR2 | VULKAN | baseline | 2 | 18.620 | 72.053 | 78.370 | 59.996 |
| PGR2 | VULKAN | candidate | 2 | 20.778 | 65.152 | 71.294 | 59.999 |

## Matched XISO group timing

One run per build/renderer. A context flag requires both average and median sums to move at least 5% in the same direction. Markerless XISO timing does not determine acceptance.

| Test group | Renderer | Tests | Baseline average sum | Candidate average sum | Average delta | Baseline median sum | Candidate median sum | Median delta | Result |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 9,417,272 us | 9,752,628 us | +3.56% | 9,417,079 us | 9,752,888 us | +3.57% | Neutral/context |
| BusyPfifo | VULKAN | 2 | 10,004,050 us | 9,707,135 us | -2.97% | 10,003,834 us | 9,706,403 us | -2.97% | Neutral/context |
| CpuFloatingPoint | OPENGL | 2 | 1,247,884 us | 1,246,308 us | -0.13% | 1,246,551 us | 1,246,484 us | -0.01% | Neutral/context |
| CpuFloatingPoint | VULKAN | 2 | 1,237,535 us | 1,244,507 us | +0.56% | 1,237,234 us | 1,245,048 us | +0.63% | Neutral/context |
| CpuTranslationBlocks | OPENGL | 3 | 522,021 us | 520,472 us | -0.30% | 520,741 us | 520,943 us | +0.04% | Neutral/context |
| CpuTranslationBlocks | VULKAN | 3 | 524,026 us | 519,449 us | -0.87% | 523,944 us | 519,542 us | -0.84% | Neutral/context |
| FillRate | OPENGL | 2 | 2,940 us | 3,043 us | +3.50% | 2,454 us | 2,544 us | +3.67% | Neutral/context |
| FillRate | VULKAN | 2 | 4,649 us | 4,513 us | -2.93% | 3,017 us | 2,968 us | -1.62% | Neutral/context |
| GameLoadComposite | OPENGL | 52 | 10,817,965 us | 10,639,709 us | -1.65% | 10,823,461 us | 10,640,509 us | -1.69% | Neutral/context |
| GameLoadComposite | VULKAN | 52 | 12,143,688 us | 11,410,731 us | -6.04% | 12,136,023 us | 11,411,174 us | -5.97% | Improve flag |
| High vertex count | OPENGL | 4 | 207,713 us | 208,710 us | +0.48% | 207,395 us | 208,438 us | +0.50% | Neutral/context |
| High vertex count | VULKAN | 4 | 208,410 us | 206,897 us | -0.73% | 208,121 us | 205,840 us | -1.10% | Neutral/context |
| PFIFOArrayElements | OPENGL | 3 | 3,863 us | 3,754 us | -2.82% | 3,903 us | 3,856 us | -1.20% | Neutral/context |
| PFIFOArrayElements | VULKAN | 3 | 3,350 us | 3,225 us | -3.73% | 2,389 us | 2,444 us | +2.30% | Neutral/context |
| PipelineTextureSwitch | OPENGL | 4 | 56,595 us | 56,746 us | +0.27% | 56,015 us | 56,566 us | +0.98% | Neutral/context |
| PipelineTextureSwitch | VULKAN | 4 | 85,029 us | 83,162 us | -2.20% | 82,800 us | 82,246 us | -0.67% | Neutral/context |
| PrimitiveType | OPENGL | 20 | 17,824 us | 17,667 us | -0.88% | 16,720 us | 16,674 us | -0.28% | Neutral/context |
| PrimitiveType | VULKAN | 20 | 21,946 us | 21,856 us | -0.41% | 16,790 us | 16,979 us | +1.13% | Neutral/context |
| ReportQuery | OPENGL | 6 | 3,812 us | 3,266 us | -14.32% | 3,714 us | 3,321 us | -10.58% | Improve flag |
| ReportQuery | VULKAN | 6 | 10,072 us | 9,668 us | -4.01% | 9,165 us | 9,015 us | -1.64% | Neutral/context |
| SurfaceRendering | OPENGL | 26 | 387,708 us | 392,610 us | +1.26% | 374,532 us | 377,938 us | +0.91% | Neutral/context |
| SurfaceRendering | VULKAN | 26 | 926,659 us | 918,804 us | -0.85% | 884,266 us | 879,846 us | -0.50% | Neutral/context |
| TinyDraw | OPENGL | 8 | 332,736 us | 337,417 us | +1.41% | 332,593 us | 337,559 us | +1.49% | Neutral/context |
| TinyDraw | VULKAN | 8 | 301,353 us | 295,694 us | -1.88% | 301,227 us | 297,364 us | -1.28% | Neutral/context |
| UniformThrash | OPENGL | 1 | 1,693 us | 1,837 us | +8.51% | 1,746 us | 1,743 us | -0.17% | Neutral/context |
| UniformThrash | VULKAN | 1 | 3,244 us | 3,195 us | -1.51% | 2,482 us | 2,465 us | -0.68% | Neutral/context |
| Vertex buffer allocation | OPENGL | 9 | 844,004 us | 858,287 us | +1.69% | 842,613 us | 856,879 us | +1.69% | Neutral/context |
| Vertex buffer allocation | VULKAN | 9 | 848,584 us | 844,243 us | -0.51% | 853,416 us | 845,243 us | -0.96% | Neutral/context |

## PGR2 resource impact

Two 60-second runs per build/renderer.

| Renderer | Build | CPU host % | Process GPU % | Device GPU % | Private MiB | Dedicated GPU MiB | Shared GPU MiB | Power W |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| OPENGL | baseline | 20.249 | 42.738 | 36.200 | 2337.867 | 210.406 | 41.625 | 23.468 |
| OPENGL | candidate | 20.690 | 42.701 | 36.800 | 2335.826 | 209.781 | 41.625 | 23.305 |
| VULKAN | baseline | 16.960 | 42.253 | 36.800 | 7116.196 | 3420.589 | 1731.976 | 19.965 |
| VULKAN | candidate | 17.313 | 43.436 | 37.650 | 7117.309 | 3421.216 | 1731.976 | 19.898 |

## Known parent failures

- VULKAN: 6x VUID-vkCmdCopyBufferToImage-imageSubresource-07971, device loss; fixed downstream by PR 15.
- OPENGL: 3 bordered-mip cubemap leaves failed 23/24 samples each; unbordered sub-block face-stride leaf failed 10/12 samples; fixed downstream by PR 14.

Full rows are in `xiso-expanded-per-test.csv`, `xiso-expanded-group-summary.csv`, `xiso-per-test.csv`, `xiso-results.csv`, `xiso-group-summary.csv`, and `retail-results.csv`.
