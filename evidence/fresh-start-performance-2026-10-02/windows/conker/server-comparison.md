A 717de396f5a6 -> B a89521cb6ff7 | partial
Attempts 4/4; blocked 0. Reference: explicit A.
Scope: 8 explicitly selected archived attempts; qualification and unrelated runs are not pooled.

### fresh-windows-conker-balanced-v2 / Reported workload metrics / b831c22ab157
| Metric | A median | B median | A mean [min,max] | B mean [min,max] | n A/B | Result | Improvement % |
|---|---:|---:|---:|---:|---:|---|---:|
| guest/cadence_fps (fps) | 29.18 | 30.00 | 29.15 [28.76,29.46] | 30.00 [30.00,30.00] | 4/4 | improved | +2.80% |
| guest/flip_elapsed_s (s) | 25.08 | 25.10 | 25.09 [25.07,25.12] | 25.12 [25.10,25.17] | 4/4 | changed | N/A |
| guest/flip_frames (frames) | 732.00 | 753.00 | 731.25 [721.00,740.00] | 753.50 [753.00,755.00] | 4/4 | changed | N/A |
| guest/interval_max_ms (ms) | 50.89 | 34.16 | 51.34 [50.23,53.34] | 34.23 [33.88,34.72] | 4/4 | improved | +32.88% |
| guest/interval_mean_ms (ms) | 34.41 | 33.33 | 34.40 [33.99,34.79] | 33.33 [33.33,33.33] | 4/4 | improved | +3.13% |
| guest/interval_p50_ms (ms) | 33.34 | 33.33 | 33.34 [33.34,33.34] | 33.33 [33.33,33.34] | 4/4 | improved | +0.01% |
| guest/interval_p95_ms (ms) | 49.68 | 33.57 | 45.90 [34.31,49.93] | 33.58 [33.51,33.65] | 4/4 | improved | +32.42% |
8 additional rows omitted; use comparison CSV.
Improvement: positive is better; lower=(A-B)/A, higher=(B-A)/A. Medians and distributions are across attempts, not pooled guest samples. Descriptive, not a significance test.
