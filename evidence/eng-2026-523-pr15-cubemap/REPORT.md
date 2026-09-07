# ENG-2026-523 issue 42 qualification

Overall result: **PASS**

## Identities

- Parent: `330cb0d86fe946b795330b629c3e0844c2c10d51` (qualified PR #67 head).
- Candidate: `b33b33c38e434df8dbb71a67837f1ecb210f8479`; tree `a35aee6abe4004c4df1a048a0c46f08d37f96680`.
- Candidate Release SHA-256: `d1c014b2f07d0ecf62d6f7259493244f5b8fddae81092ecff6647ec21e1fc7ed`.
- Candidate Debug SHA-256: `1766137312cd75edd04123cbc4ecdde1947cd26f73f22585f403abc7bd40d2b7`.
- New 152-record XISO SHA-256: `94c78f0f25a9344b62f684ac7ef09b4f023ecb06c204262f47c64072e717a1f4`.
- Catalog SHA-256: `6cd068401980a6ccb9abf502e905828cf3ffa9f3a2bc47bc49ddbea614839e02`.

## Focused correctness

| Format | Parent Release | Parent VUIDs | Fixed Release | Fixed Debug |
|---|---|---:|---|---|
| DXT1 | FAILED | 6 | PASSED | PASSED |
| DXT3 | FAILED | 6 | PASSED | PASSED |
| DXT5 | FAILED | 6 | PASSED | PASSED |

The parent produces invalid Vulkan copy extents and device loss. The fixed Release and Debug builds complete all 24 oracle cells for BC1, BC2, and BC3 with zero Vulkan VUIDs.

## New 152-record XISO correctness suite

| Group | Vulkan unique tests | Vulkan checks passed | OpenGL unique tests | OpenGL checks passed |
|---|---:|---:|---:|---:|
| Busy PFIFO | 2 | 4/4 | 2 | 4/4 |
| CPU floating point | 2 | 4/4 | 2 | 4/4 |
| CPU translation blocks | 3 | 6/6 | 3 | 6/6 |
| Fill rate | 2 | 4/4 | 2 | 4/4 |
| Game-load composites | 52 | 104/104 | 52 | 104/104 |
| High vertex count | 4 | 8/8 | 4 | 8/8 |
| PFIFO array elements | 3 | 6/6 | 3 | 6/6 |
| Pipeline/texture switch | 4 | 8/8 | 4 | 8/8 |
| Primitive types | 20 | 40/40 | 20 | 40/40 |
| Report/query | 8 | 16/16 | 8 | 16/16 |
| Surface rendering | 26 | 52/52 | 26 | 52/52 |
| Texture cubemap fallback | 3 | 6/6 | 3 | 6/6 |
| Tiny draws | 8 | 16/16 | 8 | 16/16 |
| Uniform thrash | 1 | 2/2 | 1 | 2/2 |
| Vertex allocation | 9 | 18/18 | 9 | 18/18 |

All four suite cells passed; each renderer ran twice. Vulkan reported zero VUIDs. Full per-test rows: `xiso-new-results.csv`.

## Matched 147-record XISO timing context

| Group | Vulkan tests | Vulkan average delta | OpenGL tests | OpenGL average delta |
|---|---:|---:|---:|---:|
| Busy PFIFO | 2 | -2.04% | 2 | -0.14% |
| CPU floating point | 2 | -0.23% | 2 | +0.17% |
| CPU translation blocks | 3 | -1.11% | 3 | +0.04% |
| Fill rate | 2 | -3.19% | 2 | -11.70% |
| Game-load composites | 52 | -2.60% | 52 | -0.88% |
| High vertex count | 4 | -0.96% | 4 | -0.91% |
| PFIFO array elements | 3 | -0.61% | 3 | -0.83% |
| Pipeline/texture switch | 4 | -2.91% | 4 | -5.16% |
| Primitive types | 20 | +0.07% | 20 | -2.54% |
| Report/query | 6 | +0.47% | 6 | -2.74% |
| Surface rendering | 26 | +0.94% | 26 | -1.45% |
| Tiny draws | 8 | -3.61% | 8 | -2.70% |
| Uniform thrash | 1 | +2.39% | 1 | -5.51% |
| Vertex allocation | 9 | -1.36% | 9 | -1.45% |

Full per-test timing values: `xiso-comparison.csv`.

Timing limitation: Live markers are unavailable; timing is not PR-grade performance evidence.

## Retail performance summary

| Workload | Renderer | Build | Avg FPS | p95 ms | p99 ms | GPU mean | GPU peak |
|---|---|---|---:|---:|---:|---:|---:|
| Morrowind | vulkan | baseline | 26.75 | 43.42 | 53.64 | n/a | n/a |
| Morrowind | vulkan | candidate | 27.50 | 43.59 | 51.77 | n/a | n/a |
| Morrowind | opengl | baseline | 37.52 | 33.58 | 38.73 | n/a | n/a |
| Morrowind | opengl | candidate | 37.00 | 32.61 | 40.20 | n/a | n/a |
| PGR2 | vulkan | baseline | 30.00 | 39.14 | 41.31 | 36.50% | 46.50% |
| PGR2 | vulkan | candidate | 30.00 | 39.68 | 41.61 | 33.25% | 38.50% |
| PGR2 | opengl | baseline | 30.00 | 39.70 | 42.54 | 38.35% | 39.50% |
| PGR2 | opengl | candidate | 30.01 | 40.18 | 42.58 | 37.70% | 40.50% |

Full retail and resource values: `retail-results.csv`.

## Largest matched XISO slowdowns

| Renderer | Test | Average delta |
|---|---|---:|
| opengl | `surface.cpu_read_after_gpu_write` | +13.33% |
| opengl | `report_query.zero_query` | +11.41% |
| vulkan | `report_query.dma_target_switch` | +9.69% |
| opengl | `report_query.multiple_boundaries` | +8.18% |
| vulkan | `game_load.cross_title_hotpath.surface_reuse` | +8.05% |
| opengl | `surface.surface_list_lookup_002` | +7.92% |
| vulkan | `surface.vulkan_memory_pressure.stress.plateau` | +7.23% |
| vulkan | `primitive_type.triangles.fixed_function` | +6.63% |
| vulkan | `primitive_type.line_loop.fixed_function` | +6.57% |
| vulkan | `surface.vulkan_memory_pressure.representative.idle_retention` | +6.47% |

## Source and host validation

- Texture unit tests: 9/9 PASS.
- Strict checkpatch: 0 errors, 0 warnings.
- Release and Debug builds: PASS in the pinned GCC toolchain.
- All retail final-image and private-HDD cleanup checks: PASS.
- No xemu, PresentMon, or WPR process remained; host quarantine: false.

Focused receipt: `focused-correctness-receipt.json`.
