# GitHub PR 18 qualification

Status: **PASS**

- Parent: `37823ccf18263764c82ceb6698201330a22a6c65`
- Candidate: `12022674a6e5119965005d3c58d373fcbcec93fe`

## Test suite summary

| Test group | Leaf tests |
|---|---:|
| busy_pfifo | 2 |
| cpu_floating_point | 2 |
| cpu_translation_blocks | 3 |
| fill_rate | 2 |
| game_load | 52 |
| high_vertex_count | 4 |
| pfifo_array_elements | 3 |
| pipeline_texture_switch | 4 |
| primitive_type | 20 |
| report_query | 8 |
| surface | 26 |
| texture_cubemap_fallback | 3 |
| tiny_draw | 8 |
| uniform_thrash | 1 |
| vertex_buffer_allocation | 9 |
| **Total** | **147 leaves + 5 group records = 152 records** |

- Full correctness gate: 4/4 cells PASS, 152 records per cell.
- Matched parent/candidate comparison: 4/4 cells PASS, 147 records per cell.
- Vulkan validation: zero VUIDs in every Vulkan cell.
- Process cleanup: complete; host was not quarantined.

## Performance summary

Values are arithmetic means of the sealed runs. Morrowind has one run per cell; PGR2 has two runs per build/renderer in ABBA order. Signed changes are descriptive and do not establish a regression or improvement by themselves.

| Game | Renderer | Build | n | Avg FPS | p95 ms | p99 ms | Host FPS |
|---|---|---|---:|---:|---:|---:|---:|
| Morrowind | VULKAN | baseline | 1 | 27.350 | 41.792 | 48.693 |  |
| Morrowind | VULKAN | candidate | 1 | 27.423 | 45.151 | 50.609 |  |
| Morrowind | OPENGL | baseline | 1 | 38.235 | 32.599 | 37.638 |  |
| Morrowind | OPENGL | candidate | 1 | 37.587 | 33.977 | 38.010 |  |
| PGR2 | VULKAN | baseline | 2 | 30.011 | 38.904 | 41.369 | 60.004 |
| PGR2 | VULKAN | candidate | 2 | 29.997 | 40.130 | 42.024 | 59.998 |
| PGR2 | OPENGL | baseline | 2 | 29.996 | 40.892 | 43.666 | 60.000 |
| PGR2 | OPENGL | candidate | 2 | 30.009 | 41.038 | 44.171 | 59.998 |

## Observed candidate deltas

| Game | Renderer | Avg FPS | p95 | p99 | Host FPS |
|---|---|---:|---:|---:|---:|
| Morrowind | VULKAN | 0.27% | 8.04% | 3.93% |  |
| Morrowind | OPENGL | -1.69% | 4.23% | 0.99% |  |
| PGR2 | VULKAN | -0.05% | 3.15% | 1.58% | -0.01% |
| PGR2 | OPENGL | 0.04% | 0.36% | 1.16% | -0.00% |

Lower p95/p99 is favorable. Higher FPS is favorable. The single-run Morrowind tail changes require repetition before a performance verdict.

## PGR2 resource summary

Two runs per build/renderer. GPU values use process engine counters and NVIDIA device samples from the same measurement windows.

| Renderer | Build | CPU host % | Process GPU % | Device GPU % | Private MiB | Dedicated GPU MiB | Shared GPU MiB | Power W |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| VULKAN | baseline | 21.137 | 43.774 | 35.300 | 3119.127 | 675.566 | 467.712 | 21.283 |
| VULKAN | candidate | 21.694 | 42.918 | 35.100 | 3118.529 | 675.567 | 468.375 | 21.002 |
| OPENGL | baseline | 22.833 | 44.559 | 38.100 | 2283.892 | 162.238 | 41.625 | 23.788 |
| OPENGL | candidate | 22.843 | 44.264 | 38.450 | 2288.164 | 161.609 | 41.625 | 23.828 |

## Qualification limit

The exact Release build, 13 unit cases, full XISO checks, Morrowind, and PGR2 are covered. A guarded decoder/preparation test at the exact DMA/VRAM boundary is still missing, so PR #18 remains draft.

Full rows are in `xiso-results.csv` and `retail-results.csv`.
