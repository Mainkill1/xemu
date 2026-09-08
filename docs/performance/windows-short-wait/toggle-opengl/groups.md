| XISO group | Records B/C pass | Leaf records | Baseline leaf total (ms) | Candidate leaf total (ms) | Observed change |
|---|---:|---:|---:|---:|---:|
| BusyPfifo | 2/2; 2/2 | 2 | 5384.792 | 5322.322 | -1.16% |
| CpuFloatingPoint | 2/2; 2/2 | 2 | 12507.567 | 12550.752 | +0.35% |
| CpuTranslationBlocks | 3/3; 3/3 | 3 | 5236.992 | 5237.039 | +0.00% |
| FillRate | 2/2; 2/2 | 2 | 29.031 | 32.142 | +10.72% |
| GameLoadComposite | 55/55; 55/55 | 52 | 87532.786 | 88692.826 | +1.33% |
| High vertex count | 4/4; 4/4 | 4 | 1373.764 | 1370.440 | -0.24% |
| PFIFOArrayElements | 3/3; 3/3 | 3 | 32.906 | 31.392 | -4.60% |
| PipelineTextureSwitch | 4/4; 4/4 | 4 | 477.350 | 481.472 | +0.86% |
| PrimitiveType | 20/20; 20/20 | 20 | 182.657 | 191.996 | +5.11% |
| ReportQuery | 8/8; 8/8 | 8 | 17.200 | 19.450 | +13.08% |
| SurfaceRendering | 28/28; 28/28 | 26 | 2356.777 | 2465.075 | +4.60% |
| TinyDraw | 8/8; 8/8 | 8 | 3544.472 | 3711.352 | +4.71% |
| UniformThrash | 1/1; 1/1 | 1 | 17.153 | 17.797 | +3.76% |
| Vertex buffer allocation | 9/9; 9/9 | 9 | 8726.077 | 8934.424 | +2.39% |

Totals sum the per-test mean total time for leaf records only; aggregate records are counted for correctness but excluded from time sums to avoid double counting. Campaign mode is recorded below; signed timing changes do not imply statistical significance. Signed changes are observations, not established performance decisions. Full rows are in per-test.csv.

Campaign mode: **timing**. Live-marker timing contract required by both sealed campaigns.
