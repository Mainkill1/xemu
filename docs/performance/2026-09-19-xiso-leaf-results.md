# XISO per-leaf timings: 2026-09-19 Windows comparison

The [official upstream v0.8.136 release](https://github.com/xemu-project/xemu/releases/tag/v0.8.136), the published fork-main build at `ef1a7fc4`, and the #135-plus-defaults candidate at `cd3c30b6` each had two XISO attempts on the same Windows rig. The [full comparison](2026-09-19-official-upstream-comparison.md) records settings and build hashes. The source values are **guest average microseconds per XISO leaf**; the table below converts them to milliseconds. They are not frame times or displayed FPS. Lower latency is better.

A positive speedup means the newer build completed that leaf faster: `100 × (control latency / newer latency − 1)`. The two-run value compares each role's arithmetic mean latency. Run-1 and run-2 percentages in the [complete CSV](2026-09-19-xiso-leaf-comparison.csv) are descriptive index matches; runs were grouped by role rather than interleaved. The [summary JSON](2026-09-19-xiso-leaf-comparison-summary.json) makes the counts machine-readable.

**These percentages are diagnostic, not PR-grade timing proof.** The suite could not create its live guest markers, and the official upstream run terminated early with Vulkan device loss. A speedup is withheld when a leaf failed, was not reached, changed its test contract, or produced a different functional framebuffer hash. Hash equality does not by itself prove pixel-exact equivalence.

## Comparable coverage

| Comparison | Comparable leaves | Faster / slower | Median leaf speedup | Geometric mean speedup | Exclusions |
| --- | ---: | ---: | ---: | ---: | --- |
| Fork main vs official upstream | 29 | 12 / 17 | -2.07% | +2.75% | 117 not reached, 2 failed, 4 passing hash mismatches |
| Candidate vs fork main | 150 | 73 / 77 | -0.09% | +1.26% | 2 hash mismatches |

The positive geometric means are influenced by a few large wins, especially the 494.26% `s3tc_streaming_fenced_draws` leaf against upstream. The median leaf and faster/slower counts show that neither comparison is a uniform XISO speedup. The candidate-versus-main result remains mixed and does not override PR #135's separate gameplay HOLD.

## Fork main versus official upstream: all 29 comparable leaves

| XISO leaf | Official runs 1 / 2 (ms) | Main runs 1 / 2 (ms) | Main speedup |
| --- | ---: | ---: | ---: |
| `busy_pfifo.pfifo_saturation` | 67.774 / 65.593 | 105.122 / 100.687 | -35.20% |
| `busy_pfifo.pgraph_pattern_polling` | 8261.209 / 8395.558 | 4427.825 / 4582.678 | +84.86% |
| `cpu_floating_point.sse_scalar` | 512.472 / 510.971 | 518.075 / 521.383 | -1.54% |
| `cpu_floating_point.x87_scalar` | 755.892 / 758.102 | 780.180 / 787.358 | -3.42% |
| `cpu_translation_blocks.direct_loop` | 10.076 / 10.093 | 9.787 / 9.851 | +2.70% |
| `cpu_translation_blocks.indirect_dispatch` | 25.162 / 24.877 | 39.352 / 39.362 | -36.43% |
| `cpu_translation_blocks.indirect_dispatch_stress` | 497.811 / 494.841 | 786.101 / 787.302 | -36.91% |
| `fill_rate.solid` | 1.674 / 1.601 | 2.324 / 1.400 | -12.06% |
| `fill_rate.textured` | 1.986 / 1.858 | 2.365 / 1.276 | +5.58% |
| `game_load.cross_title_hotpath.blend_constant_reuse` | 102.941 / 100.940 | 116.136 / 116.406 | -12.33% |
| `game_load.cross_title_hotpath.gpu_wait_control` | 2.111 / 1.896 | 1.294 / 1.253 | +57.32% |
| `game_load.cross_title_hotpath.pgr2_lagspot_inline_elements` | 132.705 / 133.087 | 105.336 / 104.051 | +26.94% |
| `game_load.cross_title_hotpath.pgr2_small_draws` | 107.781 / 107.623 | 115.080 / 114.861 | -6.32% |
| `game_load.cross_title_hotpath.pipeline_state_churn` | 50.964 / 51.597 | 67.027 / 66.948 | -23.45% |
| `game_load.cross_title_hotpath.queued_vertex_cpu_writes` | 62.397 / 53.893 | 66.782 / 64.883 | -11.68% |
| `game_load.cross_title_hotpath.s3tc_streaming_fenced_draws` | 7058.658 / 6643.692 | 1148.507 / 1157.278 | +494.26% |
| `game_load.cross_title_hotpath.scaled_surface_pressure` | 352.345 / 398.352 | 348.297 / 343.837 | +8.46% |
| `game_load.cross_title_hotpath.surface_reuse` | 5.711 / 5.569 | 6.481 / 5.918 | -9.02% |
| `game_load.cross_title_hotpath.texture_binding_reuse` | 183.075 / 183.635 | 154.190 / 154.506 | +18.79% |
| `game_load.cross_title_hotpath.texture_update_reuse` | 3.517 / 3.521 | 2.920 / 2.778 | +23.52% |
| `game_load.long_unlocked_scene.alpha_overdraw` | 29.689 / 29.483 | 33.106 / 33.600 | -11.29% |
| `game_load.long_unlocked_scene.combined` | 59.921 / 60.630 | 70.551 / 71.340 | -15.04% |
| `game_load.long_unlocked_scene.cpu` | 15.988 / 16.238 | 23.811 / 23.789 | -32.30% |
| `game_load.long_unlocked_scene.full_system` | 59.635 / 59.047 | 70.837 / 71.242 | -16.47% |
| `game_load.long_unlocked_scene.pfifo` | 6.163 / 6.209 | 4.930 / 4.957 | +25.13% |
| `game_load.long_unlocked_scene.streaming_surface_reuse` | 8.883 / 8.611 | 9.007 / 8.856 | -2.07% |
| `game_load.s3tc_sync_factor.dxt1_dirty_once_redraw` | 15.770 / 16.029 | 15.720 / 15.544 | +1.71% |
| `game_load.s3tc_sync_factor.rgba8_dirty_once_redraw` | 16.201 / 16.133 | 16.250 / 16.408 | -0.99% |
| `game_load.s3tc_sync_factor.rgba8_ring_payload_generations` | 49.413 / 49.577 | 49.105 / 49.314 | +0.58% |

The two upstream functional failures were `game_load.s3tc_sync_factor.dxt1_same_address_wait` and `game_load.s3tc_sync_factor.rgba8_same_address_wait`. Four additional passing leaves had different framebuffer hashes and are excluded from the speed table:

- `game_load.s3tc_sync_factor.bc2_native_eligible`
- `game_load.s3tc_sync_factor.dxt1_ring_payload_generations`
- `game_load.s3tc_sync_factor.dxt1_same_address_queued`
- `game_load.s3tc_sync_factor.rgba8_same_address_queued`

The other 117 leaves were not reached by official upstream. The [CSV](2026-09-19-xiso-leaf-comparison.csv) gives every one of the 152 leaves a status, both raw run values where available, and run-specific plus two-run percentage columns. It also contains all 150 hash-matched candidate-versus-main leaf comparisons.
