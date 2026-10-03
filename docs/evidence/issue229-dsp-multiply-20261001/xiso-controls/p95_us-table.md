| Test ID | Backend | Before ms | After ms | Saved ms | Improvement | Correctness |
|---|---|---:|---:|---:|---:|---|
| `cpu_floating_point.sse_scalar` | Vulkan | 1189.357 | 1204.724 | -15.367 | -1.29% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Vulkan | 20.604 | 20.543 | +0.061 | +0.30% | 8/8 leaf checks pass |
| `cpu_floating_point.sse_scalar` | Opengl | 1197.518 | 1215.927 | -18.409 | -1.54% | 8/8 leaf checks pass |
| `cpu_translation_blocks.direct_loop` | Opengl | 20.952 | 22.105 | -1.153 | -5.50% | 8/8 leaf checks pass |
