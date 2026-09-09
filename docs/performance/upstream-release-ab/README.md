# Current upstream versus combined PRs — Release performance review

Status: measured comparison; integration remains draft pending review.
PR: https://github.com/Mainkill1/xemu/pull/52
Baseline source: `fdfb5a8f481b2f870c57080e74ec8d3a31a47053` (frozen upstream master).
Candidate source: `2af783f2a49774fd7477f6f1124916418390f259` (the requested 16 open PRs plus merged #47/#50).
Baseline executable SHA-256: `2be4252b8e08cf0459c5de760fac35b9078761e5d75339ce64b7a071026fbbe1`.
Candidate executable SHA-256: `c82380034a6e5a134cf1f0c20f76cbb8db3bef813cd55a29324ea07112de18c9`.

## Goal

Compare the frozen upstream source with the combined fork changes using identical optimized Windows builds and existing automated workloads. This comparison does not attribute an aggregate change to an individual PR.

## Build and measurement scope

Both builds use the same pinned GCC 16.1 toolchain, explicit Meson Release, `-O3`, full LTO, x86-64-v3, debug symbols disabled, QOM cast debugging disabled, and stripped executables. Required assertions remain enabled. No fast-math option was added. The earlier debug-symbol campaign is superseded and excluded.
PGR2 uses the existing fresh-boot keyboard route: two runs per build and renderer in ABBA order, with 60-second capture windows. Morrowind uses the immutable snapshot and existing five-second wait, Start, two-second wait, B, two-second wait sequence, with a ten-second capture per build and renderer.
XISO runs every catalog group on both renderers. If either group run fails, its execution targets are run individually once on both builds. Only passing, identically selected records with matching framebuffer hashes contribute to comparisons; failures remain N/A. Vulkan validation was not enabled during performance capture.

## Performance Summary

Guest FPS values below are **NV2A display-write cadence**, not rendered or host-presented FPS. P95/p99 are guest display-write intervals in milliseconds. PGR2 summary values are averages of per-run measurements, not pooled percentiles.

| Workload / metric | Renderer | Baseline | Candidate | Delta |
| --- | --- | ---: | ---: | ---: |
| PGR2 / display-write cadence (FPS) | OPENGL | 5.705 | 30.000 | +425.86% |
| PGR2 / p95 interval (ms) | OPENGL | 244.303 | 39.573 | -83.80% |
| PGR2 / p99 interval (ms) | OPENGL | 272.576 | 42.558 | -84.39% |
| PGR2 / display-write cadence (FPS) | VULKAN | 4.636 | 30.001 | +547.10% |
| PGR2 / p95 interval (ms) | VULKAN | 285.530 | 39.166 | -86.28% |
| PGR2 / p99 interval (ms) | VULKAN | 325.062 | 41.345 | -87.28% |
| Morrowind / display-write cadence (FPS) | OPENGL | 38.300 | 38.423 | +0.32% |
| Morrowind / p95 interval (ms) | OPENGL | 31.876 | 31.856 | -0.06% |
| Morrowind / p99 interval (ms) | OPENGL | 37.299 | 36.530 | -2.06% |
| Morrowind / display-write cadence (FPS) | VULKAN | 18.696 | 27.688 | +48.09% |
| Morrowind / p95 interval (ms) | VULKAN | 63.821 | 41.936 | -34.29% |
| Morrowind / p99 interval (ms) | VULKAN | 73.365 | 48.707 | -33.61% |

Small signed differences are observed directions only. No statistical confidence interval or general performance acceptance claim is inferred from these limited repetitions.

## Test Results

Campaign completion status: `PASS`. A completed campaign may contain explicitly recorded N/A XISO outcomes.

| Renderer | Baseline passing leaves | Candidate passing leaves | Comparable leaves | N/A comparisons |
| --- | ---: | ---: | ---: | ---: |
| opengl | 143 | 147 | 135 | 12 |
| vulkan | 130 | 147 | 130 | 17 |

### Test Suite Summary

Grouped XISO timings are sums of matched leaf averages/medians in microseconds. They are not full-suite wall-clock duration. This production-artifact run lacks the suite’s live-marker eligibility evidence, so these timings provide context rather than formal performance qualification.

| Test group | Renderer | Matched / expected | Baseline average sum (µs) | Candidate average sum (µs) | Average delta | Baseline median sum (µs) | Candidate median sum (µs) | Median delta |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| busy_pfifo | opengl | 2/2 | 10,464,505.000 | 5,046,403.000 | -51.78% | 10,464,355.000 | 5,046,535.000 | -51.77% |
| busy_pfifo | vulkan | 2/2 | 8,669,294.000 | 5,909,374.000 | -31.84% | 8,668,804.000 | 5,909,341.000 | -31.83% |
| cpu_floating_point | opengl | 2/2 | 1,269,205.000 | 1,297,106.000 | +2.20% | 1,266,580.000 | 1,296,334.000 | +2.35% |
| cpu_floating_point | vulkan | 2/2 | 1,261,655.000 | 1,330,244.000 | +5.44% | 1,264,443.000 | 1,329,658.000 | +5.16% |
| cpu_translation_blocks | opengl | 3/3 | 570,004.000 | 652,593.000 | +14.49% | 572,756.000 | 650,223.000 | +13.53% |
| cpu_translation_blocks | vulkan | 3/3 | 562,528.000 | 652,355.000 | +15.97% | 559,665.000 | 654,242.000 | +16.90% |
| fill_rate | opengl | 2/2 | 3,486.000 | 3,080.000 | -11.65% | 3,023.000 | 2,781.000 | -8.01% |
| fill_rate | vulkan | 2/2 | 4,769.000 | 4,804.000 | +0.73% | 3,022.000 | 2,941.000 | -2.68% |
| game_load | opengl | 44/52 | 9,762,572.000 | 10,110,190.000 | +3.56% | 9,733,933.000 | 10,105,993.000 | +3.82% |
| game_load | vulkan | 40/52 | 10,636,201.000 | 7,157,599.000 | -32.71% | 10,605,626.000 | 7,104,479.000 | -33.01% |
| high_vertex_count | opengl | 4/4 | 316,269.000 | 360,385.000 | +13.95% | 315,185.000 | 361,322.000 | +14.64% |
| high_vertex_count | vulkan | 4/4 | 319,716.000 | 291,470.000 | -8.83% | 317,602.000 | 292,955.000 | -7.76% |
| pfifo_array_elements | opengl | 3/3 | 3,817.000 | 3,561.000 | -6.71% | 3,537.000 | 3,424.000 | -3.19% |
| pfifo_array_elements | vulkan | 3/3 | 3,998.000 | 4,049.000 | +1.28% | 2,425.000 | 2,510.000 | +3.51% |
| pipeline_texture_switch | opengl | 4/4 | 69,002.000 | 67,925.000 | -1.56% | 69,065.000 | 67,731.000 | -1.93% |
| pipeline_texture_switch | vulkan | 4/4 | 67,455.000 | 75,472.000 | +11.88% | 67,953.000 | 76,652.000 | +12.80% |
| primitive_type | opengl | 20/20 | 23,448.000 | 23,404.000 | -0.19% | 22,472.000 | 22,479.000 | +0.03% |
| primitive_type | vulkan | 20/20 | 24,420.000 | 23,241.000 | -4.83% | 18,943.000 | 18,523.000 | -2.22% |
| report_query | opengl | 7/8 | 15,262.000 | 13,757.000 | -9.86% | 10,854.000 | 9,707.000 | -10.57% |
| report_query | vulkan | 6/8 | 36,441.000 | 37,953.000 | +4.15% | 21,120.000 | 23,896.000 | +13.14% |
| surface | opengl | 26/26 | 390,198.000 | 347,655.000 | -10.90% | 372,897.000 | 331,517.000 | -11.10% |
| surface | vulkan | 26/26 | 590,012.000 | 517,315.000 | -12.32% | 569,668.000 | 491,436.000 | -13.73% |
| texture_cubemap_fallback | opengl | 0/3 | N/A | N/A | N/A | N/A | N/A | N/A |
| texture_cubemap_fallback | vulkan | 0/3 | N/A | N/A | N/A | N/A | N/A | N/A |
| tiny_draw | opengl | 8/8 | 359,847.000 | 348,775.000 | -3.08% | 359,219.000 | 348,533.000 | -2.97% |
| tiny_draw | vulkan | 8/8 | 246,480.000 | 205,965.000 | -16.44% | 246,687.000 | 206,558.000 | -16.27% |
| uniform_thrash | opengl | 1/1 | 1,646.000 | 1,795.000 | +9.05% | 1,552.000 | 1,586.000 | +2.19% |
| uniform_thrash | vulkan | 1/1 | 3,028.000 | 3,218.000 | +6.27% | 1,915.000 | 2,024.000 | +5.69% |
| vertex_buffer_allocation | opengl | 9/9 | 1,953,574.000 | 1,970,494.000 | +0.87% | 1,951,465.000 | 1,968,922.000 | +0.89% |
| vertex_buffer_allocation | vulkan | 9/9 | 1,621,537.000 | 1,188,607.000 | -26.70% | 1,617,383.000 | 1,188,970.000 | -26.49% |

All individual results and exclusions: [per-test CSV](xiso-per-test.csv). Original group failures and bounded isolation attempts: [attempt CSV](xiso-attempts.csv). No failing group attempts were erased by fallback testing.

## Resource Impact

PGR2 resource values are means of the existing collector summaries. Peak fields, sample counts and collector errors remain in the per-run CSV. GPU power and clocks use samples inside each measurement window. Power changes must be interpreted alongside cadence and utilization.

| PGR2 metric | Renderer | Baseline | Candidate | Delta |
| --- | --- | ---: | ---: | ---: |
| CPU (% host capacity) | OPENGL | 19.835 | 17.735 | -10.59% |
| Process GPU engine utilization (%) | OPENGL | 34.486 | 44.436 | +28.85% |
| Working set (MiB) | OPENGL | 676.660 | 682.683 | +0.89% |
| Private memory (MiB) | OPENGL | 2,335.673 | 2,285.065 | -2.17% |
| Dedicated GPU memory (MiB) | OPENGL | 212.970 | 162.867 | -23.53% |
| Shared GPU memory (MiB) | OPENGL | 41.625 | 41.713 | +0.21% |
| GPU power (W) | OPENGL | 18.979 | 24.196 | +27.49% |
| GPU graphics clock (MHz) | OPENGL | 222.750 | 686.250 | +208.08% |
| GPU memory clock (MHz) | OPENGL | 668.250 | 6,001.000 | +798.02% |
| CPU (% host capacity) | VULKAN | 18.149 | 16.385 | -9.72% |
| Process GPU engine utilization (%) | VULKAN | 29.032 | 43.836 | +50.99% |
| Working set (MiB) | VULKAN | 2,240.410 | 1,001.156 | -55.31% |
| Private memory (MiB) | VULKAN | 7,096.425 | 3,100.494 | -56.31% |
| Dedicated GPU memory (MiB) | VULKAN | 3,425.645 | 675.604 | -80.28% |
| Shared GPU memory (MiB) | VULKAN | 1,730.176 | 467.090 | -73.00% |
| GPU power (W) | VULKAN | 18.727 | 21.154 | +12.96% |
| GPU graphics clock (MHz) | VULKAN | 274.500 | 451.500 | +64.48% |
| GPU memory clock (MHz) | VULKAN | 755.050 | 2,367.300 | +213.53% |

## Unfavorable observations and unresolved output differences

The aggregate remains **draft**. The following timing increases are visible in the matched measurements; they are not hidden by the retail gains. The XISO eligibility limitation above still applies. No attribution to a particular patch is established.

| Case | Renderer | Baseline average (µs) | Candidate average (µs) | Delta |
| --- | --- | ---: | ---: | ---: |
| game_load.pgr2_ai_backup.cpu_only | vulkan | 27,967.000 | 62,686.000 | +124.14% |
| game_load.doax_menu_stress.cpu_only | vulkan | 20,388.000 | 43,390.000 | +112.82% |
| game_load.doax_menu_representative.cpu_only | vulkan | 10,369.000 | 20,974.000 | +102.28% |
| high_vertex_count.arrays | opengl | 121.000 | 159.000 | +31.40% |
| game_load.doax_menu_stress.cpu_pfifo_gpu_streaming | vulkan | 99,511.000 | 121,722.000 | +22.32% |
| pfifo_array_elements.array_element16 | vulkan | 524.000 | 640.000 | +22.14% |
| tiny_draw.arrays.vertex_shader | vulkan | 19,107.000 | 23,014.000 | +20.45% |
| tiny_draw.inline_elements.vertex_shader | vulkan | 19,321.000 | 23,218.000 | +20.17% |

8 OpenGL leaf cases report PASS on both builds but have different framebuffer hashes. These are **unresolved output differences**, not established upstream failures or accepted correctness. Their timings are excluded pending source/oracle review. The other 21 excluded comparisons have unsuccessful upstream results.

| Output difference | Renderer |
| --- | --- |
| game_load.s3tc_sync_factor.bc2_bordered_fallback | opengl |
| game_load.s3tc_sync_factor.bc2_native_eligible | opengl |
| game_load.s3tc_sync_factor.bc3_bordered_fallback | opengl |
| game_load.s3tc_sync_factor.bc3_native_eligible | opengl |
| game_load.s3tc_sync_factor.dxt1_ring_payload_generations | opengl |
| game_load.s3tc_sync_factor.dxt1_same_address_queued | opengl |
| game_load.s3tc_sync_factor.dxt1_same_address_wait | opengl |
| game_load.s3tc_sync_factor.rgba8_same_address_queued | opengl |

All twelve retail end captures were reviewed: PGR2 reached the expected stationary-car race scene; Morrowind reached the expected unpaused snapshot scene. No controller/pause overlay or obvious corruption was visible in those end captures. This is not continuous visual validation.
All twelve native executables returned zero, all retail private-HDD cleanup checks passed, and the campaign ended with no test processes left running. See [native results](native-test-results.json) and [scene review](scene-review.json).
The candidate uses the imported feature defaults, including the enabled CPU-saving wait. This run does not qualify every individual tweak On/Off combination. The substantially slower upstream PGR2 result after the Release rebuild remains an observation requiring attribution; no claim that debug symbols caused it is supported.
Higher GPU power accompanied much higher PGR2 cadence and different GPU clock states. These measurements do not establish worse energy per delivered frame or extra redundant GPU work.

## Correctness and limitations

Host-presented PGR2 timing remains in the per-run CSV and must not be substituted for guest progression. Morrowind resource utilization was not collected. No graphs were requested.

## Evidence

[Retail runs](retail-per-run.csv) · [Resource runs](resources-per-run.csv) · [XISO groups](xiso-groups.csv) · [XISO leaves](xiso-per-test.csv) · [Attempts](xiso-attempts.csv) · [Manifest](manifest.json)

[Exact build/recipe identities](build-manifest.json). Both builds were measured on the same 16-logical-processor Windows host with the High performance power plan.
