# Historical Morrowind results relevant to the current flag

Historical records from the September 6 S3TC, cubemap-copy, and DMA-validation
qualifications contain additional measurements that precede the current
integration campaign. These records were inspected, not rerun. Their original
source/executable identities are retained in [the historical CSV](morrowind-historical.csv)
and [receipt provenance](morrowind-historical-manifest.json).

| Qualification | Baseline source | Candidate source | Vulkan baseline cadence | Vulkan candidate cadence |
| --- | --- | --- | ---: | ---: |
| S3TC/shared layout | 3caf5d85 | 330cb0d8 | 26.691346 | 27.627903 |
| Vulkan cubemap crop | 330cb0d8 | b33b33c3 | 26.745478 | 27.502532 |
| DMA validation | b33b33c3 | 58842431 | 26.893744 | 26.922979 |

The first baseline executable SHA-256 is
`fe8047f60181672170524cf40b2151f335f0e232d749b569b60fd5bc2cb5d6ba`,
**the same executable** used in the recent 28.545524 and 28.782280 baseline
observations. Historical cadence around 26.7 therefore does not require the
new candidate cleanup or even the S3TC/shared-layout additions.

Source comparison confirms that `330cb0d8` has the old populated-level cleanup,
not the 96-slot destructor. Its only production changes from cumulative S are
in GL texture upload and shared texture shape/layout code. This further weakens
attribution of current Morrowind cadence to the new cleanup cost. It does not
prove that shared layout code caused a slowdown: its original matched pair
moved in the favorable direction.

All values are display-write cadence proxies, not rendered FPS. Cross-date
measurements do not establish equivalent host load, clock/power state, or
randomized order. The old receipts do not seal the current runner hash, so
these rows must not be pooled as a controlled current-head experiment. They
establish historical behavior and a wider observed range for the same baseline
binary. The current unfavorable Vulkan pairs remain visible and integration
remains held; no statistical non-inferiority claim follows from this history.

## Implementation route

No cleanup optimization has been applied to the emulator. A new optimization
must have its own focused PR against an explicit parent, failing/positive
correctness controls where applicable, an exact-source optimized build, and
matched automated output/progression/performance tests. A cleanup microbenchmark
alone cannot qualify a game improvement. Keep the proven snapshot/keyboard
runner and released S unchanged, reuse historical evidence only within its
source/scope, and retain failed or neutral results. Do not add unrelated
optimizations directly to the release branch or existing correctness PRs.
