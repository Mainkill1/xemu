# GitHub PR 15 qualification

Status: **PASS**

- Baseline: `eb338262714ef0b1e23fd60a2c168300443377ac`
- Candidate: `cb89434e45a5191eda85abba25812192fe1c9b21`
- Baseline executable SHA-256: `4aca1e0b02526a2205d6c85d6d78415328dc60e62578cd02157f37fda1323a61`
- Candidate executable SHA-256: `44099336b62b34d62295a6b936b03cd00e1f479ef0972c8a36ef73f8fccd2c97`
- Expanded XISO/catalog SHA-256: `1426632c6c37005a724184329ed0de4a3cf7dc75a9102c0e9ba255380a93a31f` / `a0007a04191d6900b861e227f9110ba856857e68847bb683452b50ec7d2b50f2`
- Matched XISO/catalog SHA-256: `08551d0c0b7bc5efb20a7b36d6d4f0e24ab666b4cee25930232858ccd9872a3e` / `027065948624d6aafdbe557bed8123eb6dcaa83353cf242c71d030a109b64578`

## Test suite summary

| Suite | OpenGL | Vulkan |
|---|---|---|
| Expanded 153-record suite, two runs | PASS | PASS |
| Matched 147-record suite | PASS | PASS |

- Morrowind: PASS, 4/4 cells.
- PGR2: PASS, 8/8 ABBA cells.
- Cleanup: PASS; host not quarantined.

### Expanded 153-record groups

| Test group | Renderer | Tests | Runs | Passed checks | Result |
|---|---|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 2 | 4/4 | PASS |
| BusyPfifo | VULKAN | 2 | 2 | 4/4 | PASS |
| CpuFloatingPoint | OPENGL | 2 | 2 | 4/4 | PASS |
| CpuFloatingPoint | VULKAN | 2 | 2 | 4/4 | PASS |
| CpuTranslationBlocks | OPENGL | 3 | 2 | 6/6 | PASS |
| CpuTranslationBlocks | VULKAN | 3 | 2 | 6/6 | PASS |
| FillRate | OPENGL | 2 | 2 | 4/4 | PASS |
| FillRate | VULKAN | 2 | 2 | 4/4 | PASS |
| GameLoadComposite | OPENGL | 52 | 2 | 104/104 | PASS |
| GameLoadComposite | VULKAN | 52 | 2 | 104/104 | PASS |
| High vertex count | OPENGL | 4 | 2 | 8/8 | PASS |
| High vertex count | VULKAN | 4 | 2 | 8/8 | PASS |
| PFIFOArrayElements | OPENGL | 3 | 2 | 6/6 | PASS |
| PFIFOArrayElements | VULKAN | 3 | 2 | 6/6 | PASS |
| PipelineTextureSwitch | OPENGL | 4 | 2 | 8/8 | PASS |
| PipelineTextureSwitch | VULKAN | 4 | 2 | 8/8 | PASS |
| PrimitiveType | OPENGL | 20 | 2 | 40/40 | PASS |
| PrimitiveType | VULKAN | 20 | 2 | 40/40 | PASS |
| ReportQuery | OPENGL | 8 | 2 | 16/16 | PASS |
| ReportQuery | VULKAN | 8 | 2 | 16/16 | PASS |
| SurfaceRendering | OPENGL | 26 | 2 | 52/52 | PASS |
| SurfaceRendering | VULKAN | 26 | 2 | 52/52 | PASS |
| TextureCubemapFallback | OPENGL | 4 | 2 | 8/8 | PASS |
| TextureCubemapFallback | VULKAN | 4 | 2 | 8/8 | PASS |
| TinyDraw | OPENGL | 8 | 2 | 16/16 | PASS |
| TinyDraw | VULKAN | 8 | 2 | 16/16 | PASS |
| UniformThrash | OPENGL | 1 | 2 | 2/2 | PASS |
| UniformThrash | VULKAN | 1 | 2 | 2/2 | PASS |
| Vertex buffer allocation | OPENGL | 9 | 2 | 18/18 | PASS |
| Vertex buffer allocation | VULKAN | 9 | 2 | 18/18 | PASS |

## Performance summary

| Game | Renderer | Build | n | Avg FPS | p95 ms | p99 ms | Host FPS |
|---|---|---|---:|---:|---:|---:|---:|
| Morrowind | OPENGL | baseline | 1 | 37.862 | 32.730 | 38.142 |  |
| Morrowind | OPENGL | candidate | 1 | 38.494 | 31.135 | 37.467 |  |
| Morrowind | VULKAN | baseline | 1 | 27.987 | 43.244 | 46.989 |  |
| Morrowind | VULKAN | candidate | 1 | 27.410 | 44.197 | 51.634 |  |
| PGR2 | OPENGL | baseline | 2 | 30.010 | 40.165 | 42.752 | 60.001 |
| PGR2 | OPENGL | candidate | 2 | 30.004 | 39.884 | 42.704 | 59.998 |
| PGR2 | VULKAN | baseline | 2 | 29.999 | 39.248 | 41.323 | 59.999 |
| PGR2 | VULKAN | candidate | 2 | 29.999 | 40.033 | 41.962 | 60.001 |

## Matched XISO group timing

One run per build/renderer. A context flag requires both average and median sums to move at least 5% in the same direction. Markerless XISO timing does not determine acceptance.

| Test group | Renderer | Tests | Baseline average sum | Candidate average sum | Average delta | Baseline median sum | Candidate median sum | Median delta | Result |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| BusyPfifo | OPENGL | 2 | 4,532,311 us | 4,299,601 us | -5.13% | 4,532,281 us | 4,299,036 us | -5.15% | Improve flag |
| BusyPfifo | VULKAN | 2 | 5,000,365 us | 5,055,846 us | +1.11% | 5,000,222 us | 5,055,544 us | +1.11% | Neutral/context |
| CpuFloatingPoint | OPENGL | 2 | 1,239,123 us | 1,238,221 us | -0.07% | 1,239,310 us | 1,238,358 us | -0.08% | Neutral/context |
| CpuFloatingPoint | VULKAN | 2 | 1,248,357 us | 1,253,729 us | +0.43% | 1,248,587 us | 1,254,826 us | +0.50% | Neutral/context |
| CpuTranslationBlocks | OPENGL | 3 | 519,076 us | 519,614 us | +0.10% | 519,323 us | 518,998 us | -0.06% | Neutral/context |
| CpuTranslationBlocks | VULKAN | 3 | 518,425 us | 518,076 us | -0.07% | 518,000 us | 518,103 us | +0.02% | Neutral/context |
| FillRate | OPENGL | 2 | 2,933 us | 3,226 us | +9.99% | 2,593 us | 3,063 us | +18.13% | Regress flag |
| FillRate | VULKAN | 2 | 3,639 us | 3,577 us | -1.70% | 1,971 us | 1,915 us | -2.84% | Neutral/context |
| GameLoadComposite | OPENGL | 52 | 10,774,963 us | 10,569,037 us | -1.91% | 10,773,010 us | 10,565,521 us | -1.93% | Neutral/context |
| GameLoadComposite | VULKAN | 52 | 7,341,956 us | 7,418,311 us | +1.04% | 7,344,852 us | 7,399,890 us | +0.75% | Neutral/context |
| High vertex count | OPENGL | 4 | 208,245 us | 208,687 us | +0.21% | 206,898 us | 207,362 us | +0.22% | Neutral/context |
| High vertex count | VULKAN | 4 | 230,868 us | 226,153 us | -2.04% | 230,139 us | 224,051 us | -2.65% | Neutral/context |
| PFIFOArrayElements | OPENGL | 3 | 3,586 us | 3,837 us | +7.00% | 3,875 us | 3,871 us | -0.10% | Neutral/context |
| PFIFOArrayElements | VULKAN | 3 | 2,836 us | 2,820 us | -0.56% | 1,939 us | 1,968 us | +1.50% | Neutral/context |
| PipelineTextureSwitch | OPENGL | 4 | 56,828 us | 56,645 us | -0.32% | 56,560 us | 56,449 us | -0.20% | Neutral/context |
| PipelineTextureSwitch | VULKAN | 4 | 82,939 us | 82,898 us | -0.05% | 81,932 us | 81,146 us | -0.96% | Neutral/context |
| PrimitiveType | OPENGL | 20 | 17,822 us | 17,993 us | +0.96% | 16,661 us | 16,618 us | -0.26% | Neutral/context |
| PrimitiveType | VULKAN | 20 | 17,328 us | 17,012 us | -1.82% | 13,047 us | 12,885 us | -1.24% | Neutral/context |
| ReportQuery | OPENGL | 6 | 3,392 us | 3,358 us | -1.00% | 3,166 us | 3,272 us | +3.35% | Neutral/context |
| ReportQuery | VULKAN | 6 | 16,766 us | 17,513 us | +4.46% | 16,182 us | 16,190 us | +0.05% | Neutral/context |
| SurfaceRendering | OPENGL | 26 | 345,850 us | 349,492 us | +1.05% | 332,846 us | 336,633 us | +1.14% | Neutral/context |
| SurfaceRendering | VULKAN | 26 | 1,026,247 us | 1,049,350 us | +2.25% | 984,169 us | 1,003,402 us | +1.95% | Neutral/context |
| TinyDraw | OPENGL | 8 | 342,193 us | 333,000 us | -2.69% | 341,667 us | 332,009 us | -2.83% | Neutral/context |
| TinyDraw | VULKAN | 8 | 183,115 us | 182,958 us | -0.09% | 182,146 us | 183,730 us | +0.87% | Neutral/context |
| UniformThrash | OPENGL | 1 | 1,753 us | 1,767 us | +0.80% | 1,680 us | 1,763 us | +4.94% | Neutral/context |
| UniformThrash | VULKAN | 1 | 2,999 us | 2,718 us | -9.37% | 2,161 us | 1,962 us | -9.21% | Improve flag |
| Vertex buffer allocation | OPENGL | 9 | 848,923 us | 851,754 us | +0.33% | 851,740 us | 849,727 us | -0.24% | Neutral/context |
| Vertex buffer allocation | VULKAN | 9 | 895,643 us | 886,226 us | -1.05% | 901,340 us | 888,872 us | -1.38% | Neutral/context |

## PGR2 resource impact

Two 60-second runs per build/renderer.

| Renderer | Build | CPU host % | Process GPU % | Device GPU % | Private MiB | Dedicated GPU MiB | Shared GPU MiB | Power W |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| OPENGL | baseline | 22.334 | 44.277 | 37.900 | 2288.683 | 161.611 | 42.475 | 23.667 |
| OPENGL | candidate | 22.309 | 44.584 | 37.900 | 2285.394 | 160.617 | 41.625 | 23.950 |
| VULKAN | baseline | 21.324 | 45.200 | 34.350 | 3111.399 | 676.169 | 468.341 | 21.357 |
| VULKAN | candidate | 21.178 | 42.059 | 35.000 | 3110.444 | 676.182 | 467.706 | 20.952 |

## Known failures

None.

Full rows are in `xiso-expanded-per-test.csv`, `xiso-expanded-group-summary.csv`, `xiso-per-test.csv`, `xiso-results.csv`, `xiso-group-summary.csv`, and `retail-results.csv`.
