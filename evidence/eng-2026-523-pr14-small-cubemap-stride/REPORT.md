# GitHub PR 14 small-cubemap stride regression

Status: **PASS**

- Pre-repair source: `330cb0d86fe946b795330b629c3e0844c2c10d51`
- Repaired source: `eb338262714ef0b1e23fd60a2c168300443377ac`
- Test source: `e5e7466ebfcf2dca0260430bc97f47fb62dea9a4`
- Test: `TextureCubemapFallback::UnborderedSubblockDxt1`

## Result

| Build | Renderer | Expected | Observed | Failure count | Vulkan VUIDs |
|---|---|---|---|---:|---:|
| Pre-repair | OpenGL | FAIL | FAIL | 10 | |
| Repaired | OpenGL | PASS | PASS | 0 | |
| Pre-repair | Vulkan | PASS | PASS | 0 | 0 |
| Repaired | Vulkan | PASS | PASS | 0 | 0 |

The pre-repair OpenGL build addressed every 1x1 and 2x2 BC1 face from the same zero stride and observed red for all 12 samples. The repaired OpenGL build observed the six distinct face colors for both logical sizes. Vulkan passed on both builds, isolating the defect to the OpenGL face-stride calculation.

## Method

The guest oracle uploads six distinct BC1 faces at logical sizes 1x1 and 2x2. Each face occupies one aligned 8-byte BC1 block. It renders and samples each face, then emits the observed RGB565 values and a failure mask. Each matrix cell used a fresh Release xemu process. The Vulkan cells enabled validation.

The test XISO SHA-256 is `1426632c6c37005a724184329ed0de4a3cf7dc75a9102c0e9ba255380a93a31f`; its catalog SHA-256 is `a0007a04191d6900b861e227f9110ba856857e68847bb683452b50ec7d2b50f2`.

## Harness correction

All four immutable cell records completed correctly. The initial aggregate receipt remained `running` because the runner used an invalid shorthand `Where-Object` predicate during final status calculation. The predicate was corrected and the receipt was finalized by revalidating the four cell outcomes. The recovery did not change cell data.

Full XISO and retail qualification continues separately.
