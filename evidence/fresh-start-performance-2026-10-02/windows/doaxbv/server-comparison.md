A 717de396f5a6 -> B a89521cb6ff7 | partial
Attempts 4/4; blocked 0. Reference: explicit A.
Scope: 8 explicitly selected archived attempts; qualification and unrelated runs are not pooled.

### fresh-windows-doaxbv-balanced-v1 / Reported workload metrics / b831c22ab157
| Metric | A median | B median | A mean [min,max] | B mean [min,max] | n A/B | Result | Improvement % |
|---|---:|---:|---:|---:|---:|---|---:|
| guest/cadence_fps (fps) | 60.00 | 60.00 | 60.00 [60.00,60.00] | 60.00 [60.00,60.00] | 4/4 | regressed | 0.00% |
| guest/flip_elapsed_s (s) | 25.06 | 25.04 | 25.05 [25.03,25.07] | 25.04 [25.03,25.05] | 4/4 | changed | N/A |
| guest/flip_frames (frames) | 1503.50 | 1502.50 | 1503.25 [1502.00,1504.00] | 1502.50 [1502.00,1503.00] | 4/4 | changed | N/A |
| guest/interval_max_ms (ms) | 17.90 | 17.56 | 19.55 [17.70,24.69] | 17.58 [17.54,17.67] | 4/4 | improved | +1.90% |
| guest/interval_mean_ms (ms) | 16.67 | 16.67 | 16.67 [16.67,16.67] | 16.67 [16.67,16.67] | 4/4 | improved | +0.00% |
| guest/interval_p50_ms (ms) | 16.67 | 16.67 | 16.67 [16.67,16.67] | 16.67 [16.67,16.67] | 4/4 | regressed | 0.00% |
| guest/interval_p95_ms (ms) | 16.87 | 16.91 | 16.87 [16.85,16.88] | 16.91 [16.89,16.93] | 4/4 | regressed | -0.22% |
8 additional rows omitted; use comparison CSV.
Improvement: positive is better; lower=(A-B)/A, higher=(B-A)/A. Medians and distributions are across attempts, not pooled guest samples. Descriptive, not a significance test.
