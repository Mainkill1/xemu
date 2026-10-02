A 813ded9ac5e8 -> B 4a6e0dae0a83 | partial
Attempts 4/4; blocked 0. Reference: explicit A.
Scope: 8 explicitly selected archived attempts; qualification and unrelated runs are not pooled.

### fresh-deck-doaxbv-balanced-v1 / Reported workload metrics / 5adc2ea13da7
| Metric | A median | B median | A mean [min,max] | B mean [min,max] | n A/B | Result | Improvement % |
|---|---:|---:|---:|---:|---:|---|---:|
| guest/cadence_fps (fps) | 42.74 | 47.95 | 41.58 [36.53,44.30] | 47.93 [47.11,48.70] | 4/4 | improved | +12.19% |
| guest/flip_elapsed_s (s) | 25.04 | 25.05 | 25.05 [25.02,25.11] | 25.05 [25.04,25.05] | 4/4 | changed | N/A |
| guest/flip_frames (frames) | 1072.00 | 1201.00 | 1041.75 [914.00,1109.00] | 1200.50 [1180.00,1220.00] | 4/4 | changed | N/A |
| guest/interval_max_ms (ms) | 184.79 | 57.02 | 184.14 [175.79,191.20] | 67.06 [54.17,100.01] | 4/4 | improved | +69.14% |
| guest/interval_mean_ms (ms) | 23.51 | 20.68 | 24.32 [22.66,27.62] | 20.61 [20.22,20.88] | 4/4 | improved | +12.05% |
| guest/interval_p50_ms (ms) | 22.80 | 21.18 | 23.58 [21.72,27.00] | 21.09 [20.46,21.53] | 4/4 | improved | +7.08% |
| guest/interval_p95_ms (ms) | 30.23 | 24.85 | 31.21 [29.70,34.67] | 24.84 [24.70,24.96] | 4/4 | improved | +17.81% |
8 additional rows omitted; use comparison CSV.
Improvement: positive is better; lower=(A-B)/A, higher=(B-A)/A. Medians and distributions are across attempts, not pooled guest samples. Descriptive, not a significance test.
