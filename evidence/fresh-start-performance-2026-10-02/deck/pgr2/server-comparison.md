A 813ded9ac5e8 -> B 4a6e0dae0a83 | partial
Attempts 4/4; blocked 0. Reference: explicit A.
Scope: 8 explicitly selected archived attempts; qualification and unrelated runs are not pooled.

### fresh-deck-pgr2-balanced-v1 / Reported workload metrics / 5adc2ea13da7
| Metric | A median | B median | A mean [min,max] | B mean [min,max] | n A/B | Result | Improvement % |
|---|---:|---:|---:|---:|---:|---|---:|
| guest/cadence_fps (fps) | 7.38 | 7.15 | 7.39 [7.33,7.47] | 7.24 [6.88,7.78] | 4/4 | regressed | -3.18% |
| guest/flip_elapsed_s (s) | 25.41 | 25.41 | 25.37 [25.21,25.47] | 25.43 [25.33,25.56] | 4/4 | changed | N/A |
| guest/flip_frames (frames) | 187.00 | 182.00 | 187.50 [186.00,190.00] | 184.00 [175.00,197.00] | 4/4 | changed | N/A |
| guest/interval_max_ms (ms) | 309.26 | 432.41 | 307.02 [275.76,333.80] | 463.48 [370.26,618.87] | 4/4 | regressed | -39.82% |
| guest/interval_mean_ms (ms) | 137.31 | 133.63 | 137.75 [136.71,139.68] | 132.20 [122.50,139.04] | 4/4 | improved | +2.68% |
| guest/interval_p50_ms (ms) | 132.83 | 116.49 | 132.86 [131.82,133.98] | 111.30 [90.28,121.97] | 4/4 | improved | +12.31% |
9 additional rows omitted; use comparison CSV.
Improvement: positive is better; lower=(A-B)/A, higher=(B-A)/A. Medians and distributions are across attempts, not pooled guest samples. Descriptive, not a significance test.
