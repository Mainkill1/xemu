A 813ded9ac5e8 -> B 4a6e0dae0a83 | partial
Attempts 4/4; blocked 0. Reference: explicit A.
Scope: 8 explicitly selected archived attempts; qualification and unrelated runs are not pooled.

### fresh-deck-conker-balanced-v1 / Reported workload metrics / 5adc2ea13da7
| Metric | A median | B median | A mean [min,max] | B mean [min,max] | n A/B | Result | Improvement % |
|---|---:|---:|---:|---:|---:|---|---:|
| guest/cadence_fps (fps) | 9.12 | 9.59 | 9.09 [8.77,9.37] | 9.58 [9.33,9.82] | 4/4 | improved | +5.17% |
| guest/flip_elapsed_s (s) | 25.35 | 25.34 | 25.38 [25.30,25.52] | 25.36 [25.28,25.47] | 4/4 | changed | N/A |
| guest/flip_frames (frames) | 231.00 | 243.00 | 230.75 [222.00,239.00] | 243.00 [236.00,250.00] | 4/4 | changed | N/A |
| guest/interval_max_ms (ms) | 198.91 | 183.95 | 199.00 [184.35,213.83] | 185.79 [166.64,208.61] | 4/4 | improved | +7.52% |
| guest/interval_mean_ms (ms) | 110.36 | 105.05 | 110.40 [106.53,114.35] | 105.32 [102.60,108.59] | 4/4 | improved | +4.81% |
| guest/interval_p50_ms (ms) | 108.91 | 100.04 | 108.92 [101.32,116.54] | 100.05 [100.02,100.09] | 4/4 | improved | +8.14% |
9 additional rows omitted; use comparison CSV.
Improvement: positive is better; lower=(A-B)/A, higher=(B-A)/A. Medians and distributions are across attempts, not pooled guest samples. Descriptive, not a significance test.
