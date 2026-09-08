# GitHub PR 11 qualification

Status: **PASS_WITH_KNOWN_PARENT_FAILURE_AND_BOUNDED_RETRY; FINAL FAILURE CONTRACT ACCEPTED**

Final follow-up: source `a6533fd5726761f0561e0a810f691f87ad3bb93b` now checks VMA map, invalidate, and flush results in the surface-download and texture-upload paths. A scoped guard provides exactly-once unmapping on every post-map return. Seven deterministic injected contract cases passed, followed by exact-head Vulkan surface-download and texture-switch production tests with zero VUIDs. See [the mapped-failure follow-up](MAPPED-FAILURE-FOLLOWUP.md). This supersedes the earlier **Revise** decision below; the historical ordinary-path tables and known parent failures remain unchanged.

- Baseline: `208e4596832a1f949bd63822d5ddd962c4575067`
- Candidate: `021bc7293ab534c10d311761ca975985ab50a48c`
- Baseline executable SHA-256: `6c41b50acb94fc71442ae6f217c8c236ffcc3fda12ce81e7553cb391b1fd2527`
- Candidate executable SHA-256: `431d42576e31ec2a8d21ba31feed4ccce283b57c4d515b9801b8e8c32ef40f43`
- Expanded XISO/catalog SHA-256: `1426632c6c37005a724184329ed0de4a3cf7dc75a9102c0e9ba255380a93a31f` / `a0007a04191d6900b861e227f9110ba856857e68847bb683452b50ec7d2b50f2`
- Matched XISO/catalog SHA-256: `08551d0c0b7bc5efb20a7b36d6d4f0e24ab666b4cee25930232858ccd9872a3e` / `027065948624d6aafdbe557bed8123eb6dcaa83353cf242c71d030a109b64578`

## Test suite summary

| Suite | OpenGL | Vulkan |
|---|---|---|
| Expanded 153-record suite | PASS_WITH_KNOWN_PARENT_FAILURE | Known parent failure: PR 15 bordered-mip copy |
| Matched 147-record suite | PASS | PASS |

- Morrowind: PASS, 4/4 cells in one bounded retry.
- PGR2: PASS, 8/8 ABBA cells.
- Cleanup: PASS; host not quarantined.

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
| Morrowind | OPENGL | baseline | 1 | 37.953 | 32.689 | 38.757 |  |
| Morrowind | OPENGL | candidate | 1 | 35.466 | 35.083 | 40.962 |  |
| Morrowind | VULKAN | baseline | 1 | 28.350 | 41.774 | 45.417 |  |
| Morrowind | VULKAN | candidate | 1 | 27.117 | 43.120 | 52.949 |  |
| PGR2 | OPENGL | baseline | 2 | 30.000 | 39.269 | 42.361 | 59.996 |
| PGR2 | OPENGL | candidate | 2 | 30.006 | 40.164 | 43.242 | 60.000 |
| PGR2 | VULKAN | baseline | 2 | 30.006 | 39.547 | 41.834 | 60.001 |
| PGR2 | VULKAN | candidate | 2 | 30.006 | 38.802 | 40.843 | 59.999 |

## Matched XISO group timing

One run per build/renderer. A context flag requires both average and median sums to move at least 5% in the same direction. Markerless XISO timing does not determine acceptance.

| Test group | Renderer | Tests | Baseline average sum | Candidate average sum | Average delta | Baseline median sum | Candidate median sum | Median delta | Result |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 4,872,629 us | 4,791,786 us | -1.66% | 4,872,598 us | 4,791,556 us | -1.66% | Neutral/context |
| BusyPfifo | VULKAN | 2 | 5,231,812 us | 4,949,821 us | -5.39% | 5,230,944 us | 4,949,129 us | -5.39% | Improve flag |
| CpuFloatingPoint | OPENGL | 2 | 1,243,317 us | 1,243,645 us | +0.03% | 1,241,218 us | 1,243,006 us | +0.14% | Neutral/context |
| CpuFloatingPoint | VULKAN | 2 | 1,244,559 us | 1,251,588 us | +0.56% | 1,241,165 us | 1,251,812 us | +0.86% | Neutral/context |
| CpuTranslationBlocks | OPENGL | 3 | 520,130 us | 517,062 us | -0.59% | 519,383 us | 516,502 us | -0.55% | Neutral/context |
| CpuTranslationBlocks | VULKAN | 3 | 516,541 us | 517,866 us | +0.26% | 516,705 us | 517,392 us | +0.13% | Neutral/context |
| FillRate | OPENGL | 2 | 3,071 us | 2,731 us | -11.07% | 2,881 us | 2,484 us | -13.78% | Improve flag |
| FillRate | VULKAN | 2 | 3,862 us | 3,572 us | -7.51% | 2,060 us | 1,958 us | -4.95% | Neutral/context |
| GameLoadComposite | OPENGL | 52 | 10,602,598 us | 10,564,025 us | -0.36% | 10,603,328 us | 10,569,554 us | -0.32% | Neutral/context |
| GameLoadComposite | VULKAN | 52 | 7,404,114 us | 7,308,893 us | -1.29% | 7,409,094 us | 7,291,009 us | -1.59% | Neutral/context |
| High vertex count | OPENGL | 4 | 209,226 us | 205,094 us | -1.97% | 208,082 us | 204,381 us | -1.78% | Neutral/context |
| High vertex count | VULKAN | 4 | 242,529 us | 227,768 us | -6.09% | 241,676 us | 227,676 us | -5.79% | Improve flag |
| PFIFOArrayElements | OPENGL | 3 | 3,987 us | 3,888 us | -2.48% | 3,965 us | 3,865 us | -2.52% | Neutral/context |
| PFIFOArrayElements | VULKAN | 3 | 2,904 us | 2,834 us | -2.41% | 1,939 us | 1,919 us | -1.03% | Neutral/context |
| PipelineTextureSwitch | OPENGL | 4 | 56,896 us | 56,425 us | -0.83% | 56,680 us | 56,354 us | -0.58% | Neutral/context |
| PipelineTextureSwitch | VULKAN | 4 | 84,942 us | 83,043 us | -2.24% | 83,849 us | 81,251 us | -3.10% | Neutral/context |
| PrimitiveType | OPENGL | 20 | 18,494 us | 18,111 us | -2.07% | 17,256 us | 16,706 us | -3.19% | Neutral/context |
| PrimitiveType | VULKAN | 20 | 17,668 us | 16,666 us | -5.67% | 13,221 us | 12,681 us | -4.08% | Neutral/context |
| ReportQuery | OPENGL | 6 | 3,641 us | 3,689 us | +1.32% | 3,496 us | 3,499 us | +0.09% | Neutral/context |
| ReportQuery | VULKAN | 6 | 17,054 us | 16,465 us | -3.45% | 15,876 us | 15,643 us | -1.47% | Neutral/context |
| SurfaceRendering | OPENGL | 26 | 344,813 us | 344,768 us | -0.01% | 328,617 us | 330,304 us | +0.51% | Neutral/context |
| SurfaceRendering | VULKAN | 26 | 1,054,649 us | 1,056,123 us | +0.14% | 1,013,214 us | 1,013,324 us | +0.01% | Neutral/context |
| TinyDraw | OPENGL | 8 | 335,873 us | 331,450 us | -1.32% | 335,371 us | 330,255 us | -1.53% | Neutral/context |
| TinyDraw | VULKAN | 8 | 188,723 us | 179,407 us | -4.94% | 189,171 us | 178,146 us | -5.83% | Neutral/context |
| UniformThrash | OPENGL | 1 | 1,812 us | 1,728 us | -4.64% | 1,771 us | 1,736 us | -1.98% | Neutral/context |
| UniformThrash | VULKAN | 1 | 2,776 us | 2,757 us | -0.68% | 2,021 us | 2,003 us | -0.89% | Neutral/context |
| Vertex buffer allocation | OPENGL | 9 | 855,956 us | 842,856 us | -1.53% | 856,505 us | 842,045 us | -1.69% | Neutral/context |
| Vertex buffer allocation | VULKAN | 9 | 921,051 us | 883,970 us | -4.03% | 923,953 us | 887,138 us | -3.98% | Neutral/context |

## PGR2 resource impact

Two 60-second runs per build/renderer.

| Renderer | Build | CPU host % | Process GPU % | Device GPU % | Private MiB | Dedicated GPU MiB | Shared GPU MiB | Power W |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| OPENGL | baseline | 22.344 | 44.625 | 38.050 | 2341.785 | 211.408 | 41.725 | 23.782 |
| OPENGL | candidate | 22.214 | 44.575 | 38.300 | 2336.436 | 210.152 | 41.625 | 23.877 |
| VULKAN | baseline | 21.684 | 44.252 | 35.550 | 3106.944 | 674.289 | 468.348 | 21.130 |
| VULKAN | candidate | 21.484 | 42.787 | 34.650 | 3114.937 | 675.532 | 468.331 | 21.146 |

## Known parent failures

- VULKAN: 6x VUID-vkCmdCopyBufferToImage-imageSubresource-07971, device loss; fixed downstream by PR 15.
- OPENGL: 3 bordered-mip cubemap leaves failed 23/24 samples each; unbordered sub-block face-stride leaf failed 10/12 samples; fixed downstream by PR 14.

Full rows are in `xiso-expanded-per-test.csv`, `xiso-expanded-group-summary.csv`, `xiso-per-test.csv`, `xiso-results.csv`, `xiso-group-summary.csv`, and `retail-results.csv`.
