A 717de396f5a6 -> B a89521cb6ff7 | partial
Attempts 4/4; blocked 0. Reference: explicit A.
Scope: 8 explicitly selected archived attempts; qualification and unrelated runs are not pooled.

### fresh-windows-pgr2-balanced-v1 / Reported workload metrics / b831c22ab157
| Metric | A median | B median | A mean [min,max] | B mean [min,max] | n A/B | Result | Improvement % |
|---|---:|---:|---:|---:|---:|---|---:|
| guest/cadence_fps (fps) | 14.66 | 30.00 | 14.66 [14.57,14.75] | 30.00 [30.00,30.00] | 4/4 | improved | +104.67% |
| guest/flip_elapsed_s (s) | 25.19 | 25.10 | 25.19 [25.12,25.26] | 25.10 [25.07,25.13] | 4/4 | changed | N/A |
| guest/flip_frames (frames) | 369.50 | 753.00 | 369.25 [366.00,372.00] | 753.00 [752.00,754.00] | 4/4 | changed | N/A |
| guest/interval_max_ms (ms) | 93.30 | 34.33 | 93.73 [87.31,101.00] | 34.38 [34.22,34.66] | 4/4 | improved | +63.20% |
| guest/interval_mean_ms (ms) | 68.60 | 33.33 | 68.53 [67.93,68.98] | 33.33 [33.33,33.33] | 4/4 | improved | +51.41% |
| guest/interval_p50_ms (ms) | 71.16 | 33.33 | 70.92 [70.09,71.29] | 33.33 [33.33,33.33] | 4/4 | improved | +53.16% |
| guest/interval_p95_ms (ms) | 80.46 | 33.61 | 80.42 [79.86,80.92] | 33.61 [33.53,33.67] | 4/4 | improved | +58.22% |
8 additional rows omitted; use comparison CSV.
Improvement: positive is better; lower=(A-B)/A, higher=(B-A)/A. Medians and distributions are across attempts, not pooled guest samples. Descriptive, not a significance test.
