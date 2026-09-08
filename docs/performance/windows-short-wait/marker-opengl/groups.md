| XISO group | Records B/C pass | Leaf records | Baseline leaf total (ms) | Candidate leaf total (ms) | Observed change |
|---|---:|---:|---:|---:|---:|
| BusyPfifo | 2/2; 2/2 | 2 | 5312.707 | 5166.283 | -2.76% |
| CpuFloatingPoint | 2/2; 2/2 | 2 | 12488.372 | 12522.308 | +0.27% |
| CpuTranslationBlocks | 3/3; 3/3 | 3 | 5209.755 | 5232.458 | +0.44% |
| FillRate | 2/2; 2/2 | 2 | 30.974 | 31.899 | +2.99% |
| GameLoadComposite | 55/55; 55/55 | 52 | 86927.872 | 87799.163 | +1.00% |
| High vertex count | 4/4; 4/4 | 4 | 1358.416 | 1361.774 | +0.25% |
| PFIFOArrayElements | 3/3; 3/3 | 3 | 30.838 | 30.956 | +0.38% |
| PipelineTextureSwitch | 4/4; 4/4 | 4 | 475.370 | 479.849 | +0.94% |
| PrimitiveType | 20/20; 20/20 | 20 | 184.163 | 190.774 | +3.59% |
| ReportQuery | 8/8; 8/8 | 8 | 18.853 | 18.798 | -0.29% |
| SurfaceRendering | 28/28; 28/28 | 26 | 2358.881 | 2460.912 | +4.33% |
| TinyDraw | 8/8; 8/8 | 8 | 3499.162 | 3738.553 | +6.84% |
| UniformThrash | 1/1; 1/1 | 1 | 16.906 | 18.215 | +7.74% |
| Vertex buffer allocation | 9/9; 9/9 | 9 | 8675.621 | 8958.254 | +3.26% |

Totals sum the per-test mean total time for leaf records only; aggregate records are counted for correctness but excluded from time sums to avoid double counting. Campaign mode is recorded below; signed timing changes do not imply statistical significance. Signed changes are observations, not established performance decisions. Full rows are in per-test.csv.

Campaign mode: **timing**. Live-marker timing contract required by both sealed campaigns.
