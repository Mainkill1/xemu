# GitHub PR 14 qualification

Status: **PASS_WITH_KNOWN_PARENT_FAILURE**

- Baseline: `330cb0d86fe946b795330b629c3e0844c2c10d51`
- Candidate: `eb338262714ef0b1e23fd60a2c168300443377ac`
- Baseline executable SHA-256: `ee73b71288ebc30f8a5bb65f1d81cdb7dcb1edfb99c6352997fd16bcd411ab46`
- Candidate executable SHA-256: `4aca1e0b02526a2205d6c85d6d78415328dc60e62578cd02157f37fda1323a61`
- Expanded XISO/catalog SHA-256: `1426632c6c37005a724184329ed0de4a3cf7dc75a9102c0e9ba255380a93a31f` / `a0007a04191d6900b861e227f9110ba856857e68847bb683452b50ec7d2b50f2`
- Matched XISO/catalog SHA-256: `08551d0c0b7bc5efb20a7b36d6d4f0e24ab666b4cee25930232858ccd9872a3e` / `027065948624d6aafdbe557bed8123eb6dcaa83353cf242c71d030a109b64578`

## Test suite summary

| Suite | OpenGL | Vulkan |
|---|---|---|
| Expanded 153-record suite | PASS | Known parent failure: PR 15 bordered-mip copy |
| Matched 147-record suite | PASS | PASS |

- Morrowind: PASS, 4/4 cells.
- PGR2: PASS, 8/8 ABBA cells.
- Cleanup: PASS; host not quarantined.

### Expanded 153-record groups

| Test group | Renderer | Tests | Runs | Passed checks | Result |
|---|---|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 2 | 4/4 | PASS |
| BusyPfifo | VULKAN | 2 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| CpuFloatingPoint | OPENGL | 2 | 2 | 4/4 | PASS |
| CpuFloatingPoint | VULKAN | 2 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| CpuTranslationBlocks | OPENGL | 3 | 2 | 6/6 | PASS |
| CpuTranslationBlocks | VULKAN | 3 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| FillRate | OPENGL | 2 | 2 | 4/4 | PASS |
| FillRate | VULKAN | 2 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| GameLoadComposite | OPENGL | 52 | 2 | 104/104 | PASS |
| GameLoadComposite | VULKAN | 52 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| High vertex count | OPENGL | 4 | 2 | 8/8 | PASS |
| High vertex count | VULKAN | 4 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| PFIFOArrayElements | OPENGL | 3 | 2 | 6/6 | PASS |
| PFIFOArrayElements | VULKAN | 3 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| PipelineTextureSwitch | OPENGL | 4 | 2 | 8/8 | PASS |
| PipelineTextureSwitch | VULKAN | 4 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| PrimitiveType | OPENGL | 20 | 2 | 40/40 | PASS |
| PrimitiveType | VULKAN | 20 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| ReportQuery | OPENGL | 8 | 2 | 16/16 | PASS |
| ReportQuery | VULKAN | 8 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| SurfaceRendering | OPENGL | 26 | 2 | 52/52 | PASS |
| SurfaceRendering | VULKAN | 26 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| TextureCubemapFallback | OPENGL | 4 | 2 | 8/8 | PASS |
| TextureCubemapFallback | VULKAN | 4 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| TinyDraw | OPENGL | 8 | 2 | 16/16 | PASS |
| TinyDraw | VULKAN | 8 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| UniformThrash | OPENGL | 1 | 2 | 2/2 | PASS |
| UniformThrash | VULKAN | 1 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |
| Vertex buffer allocation | OPENGL | 9 | 2 | 18/18 | PASS |
| Vertex buffer allocation | VULKAN | 9 | 1 | Not collected | NOT_COLLECTED_DEVICE_LOSS |

## Performance summary

| Game | Renderer | Build | n | Avg FPS | p95 ms | p99 ms | Host FPS |
|---|---|---|---:|---:|---:|---:|---:|
| Morrowind | OPENGL | baseline | 1 | 35.113 | 35.815 | 40.133 |  |
| Morrowind | OPENGL | candidate | 1 | 38.548 | 32.581 | 37.787 |  |
| Morrowind | VULKAN | baseline | 1 | 27.582 | 42.173 | 51.164 |  |
| Morrowind | VULKAN | candidate | 1 | 27.345 | 44.595 | 51.077 |  |
| PGR2 | OPENGL | baseline | 2 | 30.006 | 40.104 | 43.120 | 60.001 |
| PGR2 | OPENGL | candidate | 2 | 29.993 | 39.773 | 42.331 | 59.994 |
| PGR2 | VULKAN | baseline | 2 | 30.011 | 39.891 | 41.751 | 60.007 |
| PGR2 | VULKAN | candidate | 2 | 30.007 | 38.585 | 41.204 | 59.996 |

## Matched XISO group timing

One run per build/renderer. A context flag requires both average and median sums to move at least 5% in the same direction. Markerless XISO timing does not determine acceptance.

| Test group | Renderer | Tests | Baseline average sum | Candidate average sum | Average delta | Baseline median sum | Candidate median sum | Median delta | Result |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 4,332,385 us | 4,549,063 us | +5.00% | 4,332,504 us | 4,549,172 us | +5.00% | Regress flag |
| BusyPfifo | VULKAN | 2 | 5,001,359 us | 5,238,248 us | +4.74% | 5,000,833 us | 5,238,090 us | +4.74% | Neutral/context |
| CpuFloatingPoint | OPENGL | 2 | 1,241,955 us | 1,240,934 us | -0.08% | 1,243,941 us | 1,241,089 us | -0.23% | Neutral/context |
| CpuFloatingPoint | VULKAN | 2 | 1,257,273 us | 1,264,822 us | +0.60% | 1,256,143 us | 1,262,997 us | +0.55% | Neutral/context |
| CpuTranslationBlocks | OPENGL | 3 | 516,252 us | 516,595 us | +0.07% | 516,354 us | 516,772 us | +0.08% | Neutral/context |
| CpuTranslationBlocks | VULKAN | 3 | 519,282 us | 518,897 us | -0.07% | 519,056 us | 518,434 us | -0.12% | Neutral/context |
| FillRate | OPENGL | 2 | 2,805 us | 3,403 us | +21.32% | 2,473 us | 3,262 us | +31.90% | Regress flag |
| FillRate | VULKAN | 2 | 3,657 us | 3,571 us | -2.35% | 1,978 us | 1,904 us | -3.74% | Neutral/context |
| GameLoadComposite | OPENGL | 52 | 10,520,985 us | 10,765,521 us | +2.32% | 10,523,725 us | 10,757,861 us | +2.22% | Neutral/context |
| GameLoadComposite | VULKAN | 52 | 7,339,381 us | 7,549,153 us | +2.86% | 7,342,040 us | 7,533,612 us | +2.61% | Neutral/context |
| High vertex count | OPENGL | 4 | 208,863 us | 211,909 us | +1.46% | 209,324 us | 211,323 us | +0.95% | Neutral/context |
| High vertex count | VULKAN | 4 | 228,837 us | 231,994 us | +1.38% | 227,924 us | 230,215 us | +1.01% | Neutral/context |
| PFIFOArrayElements | OPENGL | 3 | 3,897 us | 3,979 us | +2.10% | 3,915 us | 3,933 us | +0.46% | Neutral/context |
| PFIFOArrayElements | VULKAN | 3 | 2,784 us | 2,766 us | -0.65% | 1,943 us | 1,919 us | -1.24% | Neutral/context |
| PipelineTextureSwitch | OPENGL | 4 | 55,360 us | 56,775 us | +2.56% | 54,851 us | 56,304 us | +2.65% | Neutral/context |
| PipelineTextureSwitch | VULKAN | 4 | 83,180 us | 83,677 us | +0.60% | 80,975 us | 81,831 us | +1.06% | Neutral/context |
| PrimitiveType | OPENGL | 20 | 18,183 us | 17,961 us | -1.22% | 16,606 us | 16,986 us | +2.29% | Neutral/context |
| PrimitiveType | VULKAN | 20 | 17,025 us | 17,106 us | +0.48% | 12,912 us | 12,903 us | -0.07% | Neutral/context |
| ReportQuery | OPENGL | 6 | 4,042 us | 3,587 us | -11.26% | 3,453 us | 3,663 us | +6.08% | Discordant |
| ReportQuery | VULKAN | 6 | 17,100 us | 16,873 us | -1.33% | 16,171 us | 16,297 us | +0.78% | Neutral/context |
| SurfaceRendering | OPENGL | 26 | 348,542 us | 348,559 us | +0.00% | 336,415 us | 334,241 us | -0.65% | Neutral/context |
| SurfaceRendering | VULKAN | 26 | 1,020,847 us | 1,054,610 us | +3.31% | 977,202 us | 1,009,156 us | +3.27% | Neutral/context |
| TinyDraw | OPENGL | 8 | 334,233 us | 339,649 us | +1.62% | 335,941 us | 337,801 us | +0.55% | Neutral/context |
| TinyDraw | VULKAN | 8 | 181,574 us | 180,847 us | -0.40% | 180,751 us | 179,295 us | -0.81% | Neutral/context |
| UniformThrash | OPENGL | 1 | 1,719 us | 1,720 us | +0.06% | 1,706 us | 1,734 us | +1.64% | Neutral/context |
| UniformThrash | VULKAN | 1 | 2,741 us | 2,898 us | +5.73% | 2,002 us | 2,149 us | +7.34% | Regress flag |
| Vertex buffer allocation | OPENGL | 9 | 847,069 us | 857,743 us | +1.26% | 847,634 us | 858,269 us | +1.25% | Neutral/context |
| Vertex buffer allocation | VULKAN | 9 | 886,837 us | 892,597 us | +0.65% | 891,546 us | 897,731 us | +0.69% | Neutral/context |

## PGR2 resource impact

Two 60-second runs per build/renderer.

| Renderer | Build | CPU host % | Process GPU % | Device GPU % | Private MiB | Dedicated GPU MiB | Shared GPU MiB | Power W |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| OPENGL | baseline | 22.579 | 44.443 | 38.150 | 2289.675 | 163.115 | 41.625 | 23.904 |
| OPENGL | candidate | 22.513 | 44.523 | 39.000 | 2286.198 | 161.234 | 42.525 | 23.927 |
| VULKAN | baseline | 21.642 | 45.675 | 34.550 | 3112.291 | 674.337 | 468.391 | 21.375 |
| VULKAN | candidate | 22.052 | 44.627 | 33.300 | 3112.268 | 674.305 | 468.353 | 21.750 |

## Known parent failures

- VULKAN: 6x VUID-vkCmdCopyBufferToImage-imageSubresource-07971, device loss; fixed downstream by PR 15.

Full rows are in `xiso-expanded-per-test.csv`, `xiso-expanded-group-summary.csv`, `xiso-per-test.csv`, `xiso-results.csv`, `xiso-group-summary.csv`, and `retail-results.csv`.
