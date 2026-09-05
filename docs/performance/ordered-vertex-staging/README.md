## XISO baseline versus candidate: grouped measured results

The archived XISO results **do contain usable comparative statistics**. This analysis reads all eight sanitized run summaries and eight resource summaries: two baseline and two candidate runs per renderer. These are observed differences between validation overlays, not a production retail FPS claim or an interleaved causal benchmark.

Baseline is the stable-S validation overlay `dc96faaa3e` over `bc60883c4e`, executable SHA-256 `4585b1f02e7b7ca1d4a6badf3c3ea9653df0f77ca93aeff141eeb5194d0faae7`. Candidate is overlay `1c03b81134` over PR head `1b394cf301`, executable SHA-256 `38464bdbe1b9e3f230e479a63736d022d93842a5d27fd9e47a4c09f7e8b4f008`. The overlays differ in instrumentation scope, so these observations need a matched-overlay repeat before causal attribution.

Comparability checks passed for actual guest-image SHA-256 `10ca07f227ae4e1d03510fcc1d3237304ff4291741d4c552ab33991e71de1738`, backend, 1x surface scale, vsync off, 64 MiB guest memory, multiplier 1, experiment/environment settings, leaf IDs, revisions, iterations, sample counts and completion-mode labels. All eight runs report 149/149 PASS and functional-oracle PASS; all four Vulkan runs have active validation and zero VUIDs.

### Method and exclusions

There are 144 executable leaf records and five structural group records per run. Exclude structural records to avoid double counting. Two leaf tests have changing framebuffer hashes even between baseline repeats: `game_load.s3tc_sync_factor.dxt1_same_address_queued` and `game_load.s3tc_sync_factor.rgba8_same_address_queued`. Both remain in the per-test CSV with `included=False` and their hashes in the corresponding input JSON records; neither enters the comparable totals. This is not evidence of a candidate-only failure. The remaining **142 leaves per renderer** have matching framebuffer hashes across all four runs.

For each family, sum `guest_total_us` over included leaves within each run, then average the two run totals and convert to milliseconds. Delta is `100 × (candidate / baseline − 1)`; **negative time delta is faster, positive is slower**. These are fixed-work guest timings, not FPS or p95/p99. Family weights reflect this suite's work mix, not a game's workload. The runner itself labels guest QPC timings as triage data because completion/host attribution is not proven for every workload. Two chronological runs per build do not establish significance; all repeats are retained in the CSVs.

### Test-family timing

| Family | Leaves | OpenGL S ms | OpenGL candidate ms | Time delta | Vulkan S ms | Vulkan candidate ms | Time delta |
|---|---:|---:|---:|---:|---:|---:|---:|
| busy_pfifo | 2 | 10564.511 | 10477.024 | -0.83% | 10546.520 | 10335.407 | -2.00% |
| cpu_floating_point | 2 | 12538.779 | 12372.279 | -1.33% | 12419.947 | 12329.868 | -0.73% |
| cpu_translation_blocks | 3 | 5233.330 | 5180.887 | -1.00% | 5218.653 | 5157.327 | -1.18% |
| fill_rate | 2 | 29.418 | 29.376 | -0.14% | 47.785 | 45.837 | -4.08% |
| game_load | 50 | 87081.409 | 84302.492 | -3.19% | 86714.010 | 85590.924 | -1.30% |
| high_vertex_count | 4 | 1398.610 | 1373.535 | -1.79% | 1387.144 | 1366.065 | -1.52% |
| pfifo_array_elements | 3 | 34.281 | 32.498 | -5.20% | 26.507 | 26.185 | -1.21% |
| pipeline_texture_switch | 4 | 492.136 | 479.736 | -2.52% | 690.900 | 676.593 | -2.07% |
| primitive_type | 20 | 187.934 | 190.026 | +1.11% | 223.254 | 220.872 | -1.07% |
| report_query | 8 | 19.319 | 19.361 | +0.21% | 44.487 | 41.895 | -5.83% |
| surface | 26 | 2578.642 | 2563.079 | -0.60% | 6160.601 | 6043.258 | -1.90% |
| tiny_draw | 8 | 3589.584 | 3523.669 | -1.84% | 3084.889 | 3103.885 | +0.62% |
| uniform_thrash | 1 | 17.335 | 14.774 | -14.78% | 31.995 | 32.038 | +0.13% |
| vertex_buffer_allocation | 9 | 8898.574 | 8775.919 | -1.38% | 8819.531 | 9424.888 | +6.86% |
| ALL_INCLUDED | 142 | 132663.861 | 129334.655 | -2.51% | 135416.223 | 134395.042 | -0.75% |

### Resources during XISO collection

Each cell below is the arithmetic mean of the two run summaries, not a pooled or phase-aligned sample mean. Peak rows are the mean of two run peaks. GPU engine sums can exceed 100%; device GPU percentages use a different measurement. Resource sampling covers the collection interval and is not aligned to individual test families. Signed deltas and all run values are in `resources.csv`.

| Metric | OpenGL S | OpenGL candidate | Vulkan S | Vulkan candidate |
|---|---:|---:|---:|---:|
| CPU mean, % host | 15.76 | 16.02 | 15.16 | 15.18 |
| Working set mean, MiB | 523.65 | 554.39 | 2134.31 | 2138.76 |
| Private memory mean, MiB | 2170.40 | 2172.36 | 6841.61 | 6857.19 |
| Process GPU engine sum mean, % | 80.20 | 81.00 | 59.26 | 58.06 |
| Process dedicated GPU mean, MiB | 153.88 | 154.24 | 3263.43 | 3264.52 |
| Process shared GPU mean, MiB | 41.52 | 41.62 | 1736.15 | 1745.97 |
| Device GPU mean, % | 44.96 | 45.36 | 38.13 | 36.06 |
| Device VRAM mean, MiB | 482.02 | 486.18 | 3565.27 | 3521.34 |
| Device VRAM peak, MiB | 542.00 | 544.50 | 3702.50 | 3707.50 |

### Largest Vulkan per-test changes

All changes below are included, hash-matched tests; selecting both tails preserves unfavorable results. Two repeats do not establish statistical significance.

| Test | S mean ms | Candidate mean ms | Time delta |
|---|---:|---:|---:|
| vertex_buffer_allocation.disjoint_same_page | 539.913 | 371.515 | -31.19% |
| game_load.cross_title_hotpath.texture_update_reuse | 32.409 | 25.660 | -20.82% |
| report_query.dma_descriptor_rewrite | 3.350 | 2.776 | -17.13% |
| surface.vulkan_memory_pressure.stress.idle_retention | 4.732 | 4.124 | -12.84% |
| report_query.dma_range_guard | 1.718 | 1.555 | -9.43% |
| surface.vulkan_memory_pressure.representative.idle_retention | 3.083 | 3.280 | +6.39% |
| primitive_type.triangle_strip.fixed_function | 7.377 | 7.890 | +6.96% |
| primitive_type.triangle_strip.vertex_shader | 7.248 | 7.798 | +7.60% |
| vertex_buffer_allocation.tiny.inline_elements | 291.726 | 565.522 | +93.85% |
| vertex_buffer_allocation.tiny.arrays | 293.447 | 880.948 | +200.21% |

The full per-test CSV includes both renderers, every run, means, deltas and exclusion flags. Sanitized summary inputs and a SHA-256 manifest are included for recalculation. Machine paths and unrelated operational metadata are omitted; test measurements and resource values are retained unchanged. This recovers existing XISO evidence; no new emulator campaign was run. Retail performance, matched telemetry attribution and the snapshot prerequisite remain separate open gates.

To reproduce, run `python3 reproduce.py` from this directory (Python standard library only). The CSVs retain both runs for every test and resource metric. `manifest.json` records hashes of the published inputs and original private summaries. The original evidence archive remains private.
