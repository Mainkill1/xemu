| XISO group | Records B/C pass | Leaf records | Baseline leaf total (ms) | Candidate leaf total (ms) | Observed change |
|---|---:|---:|---:|---:|---:|
| BusyPfifo | 2/2; 2/2 | 2 | 5355.549 | 5540.866 | +3.46% |
| CpuFloatingPoint | 2/2; 2/2 | 2 | 12383.651 | 12453.105 | +0.56% |
| CpuTranslationBlocks | 3/3; 3/3 | 3 | 5180.979 | 5172.707 | -0.16% |
| FillRate | 2/2; 2/2 | 2 | 33.529 | 34.020 | +1.46% |
| GameLoadComposite | 55/55; 55/55 | 52 | 55442.680 | 56138.194 | +1.25% |
| High vertex count | 4/4; 4/4 | 4 | 1475.807 | 1457.373 | -1.25% |
| PFIFOArrayElements | 3/3; 3/3 | 3 | 20.686 | 20.635 | -0.24% |
| PipelineTextureSwitch | 4/4; 4/4 | 4 | 525.856 | 511.449 | -2.74% |
| PrimitiveType | 20/20; 20/20 | 20 | 161.143 | 158.926 | -1.38% |
| ReportQuery | 8/8; 8/8 | 8 | 47.691 | 43.184 | -9.45% |
| SurfaceRendering | 28/28; 28/28 | 26 | 3037.979 | 3042.729 | +0.16% |
| TinyDraw | 8/8; 8/8 | 8 | 1761.095 | 1726.725 | -1.95% |
| UniformThrash | 1/1; 1/1 | 1 | 16.456 | 15.895 | -3.42% |
| Vertex buffer allocation | 9/9; 9/9 | 9 | 8557.335 | 8417.764 | -1.63% |

Totals sum the per-test mean total time for leaf records only; aggregate records are counted for correctness but excluded from time sums to avoid double counting. Campaign mode is recorded below; signed timing changes do not imply statistical significance. Signed changes are observations, not established performance decisions. Full rows are in per-test.csv.

Campaign mode: **timing**. Live-marker timing contract required by both sealed campaigns.
