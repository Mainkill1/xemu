| Test ID | Backend | Before ms | After ms | Saved ms | Improvement | Correctness |
|---|---|---:|---:|---:|---:|---|
| `cpu_floating_point.sse_scalar` | Vulkan | 1175.756 | 1187.375 | -11.619 | -0.99% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Vulkan | 19.870 | 19.726 | +0.144 | +0.73% | 8/8 leaf checks pass |
| `cpu_floating_point.sse_scalar` | Opengl | 1178.236 | 1196.858 | -18.622 | -1.58% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Opengl | 19.891 | 20.091 | -0.200 | -1.00% | 8/8 leaf checks pass |
