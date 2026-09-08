# Stable v2 integration qualification — held

The combined candidate completed its automated campaign, but release acceptance remains **held** for an unresolved Morrowind performance flag. This run does not establish a speedup or identify the responsible patch.

## Source and build

- Candidate: `57cf3f0a4b443e8074c79710139d2b8aaf6ac0aa`; tree `da023e4fb59986de54288a9b71b238c825241806`.
- Candidate executable SHA-256: `aa3c7d0f8d0dc0b4e6156ad0840a7d0a5ce621ae2ad42bd121a3d9126d18e7de`.
- Baseline executable source: `3caf5d85423298c62344f8fe44fe9a1176b8ae81`, the recorded tested equivalent of cumulative S `208e4596832a1f949bd63822d5ddd962c4575067`.
- Baseline executable SHA-256: `fe8047f60181672170524cf40b2151f335f0e232d749b569b60fd5bc2cb5d6ba`.
- Build: official Windows GCC toolchain, Release, full LTO, x86-64-v3. Toolchain digest: `sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2`.
- Build-tool failure: the second container initially lacked the configured LTO cache directory. Recreating it allowed the unit links to finish; the successful emulator build was reused unchanged.

## Test suite summary

- Expanded XISO: 152 records per run, twice per renderer, **608/608** total; functional hash validation passed in all four runs; zero Vulkan VUIDs. These are correctness runs, not a matched XISO performance comparison.
- Native focused units: texture lifecycle 5/5, mapped Vulkan failure contracts 7/7, Windows polling 10/10.
- Morrowind: four cells; fixed snapshot, five-second Start/B resume route, ten-second measurement, presentation interval argument 0 (no override; the sealed result records effective interval 1).
- PGR2: eight cells; full fresh-boot route, 60-second measurements, baseline–candidate–candidate–baseline per renderer, presentation interval 1.
- All cells completed and reported process cleanup; retail private HDD deletion passed. Automated image checks establish nonblank output, not semantic gameplay correctness. Earlier commentary describing them as proof of the correct track or scene overstated the check.

## Performance summary

FPS below is guest cadence (Morrowind uses a display-write proxy). PGR2 values average two run-level measurements per build; p95/p99 are means of run percentiles, not pooled percentiles. Morrowind has one run per build/renderer. No confidence interval or statistical improvement is claimed.

| Workload | Renderer | Baseline FPS | Candidate FPS | FPS change | Baseline p95 ms | Candidate p95 ms | Baseline p99 ms | Candidate p99 ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| morrowind | VULKAN | 28.546 | 26.793 | -6.14% | 44.220 | 44.188 | 51.314 | 50.392 |
| morrowind | OPENGL | 38.768 | 37.640 | -2.91% | 31.761 | 33.902 | 36.907 | 38.080 |
| pgr2 | VULKAN | 30.007 | 29.997 | -0.03% | 38.992 | 38.390 | 41.602 | 41.263 |
| pgr2 | OPENGL | 30.000 | 30.001 | +0.00% | 39.972 | 40.204 | 43.284 | 42.690 |

## Remaining problem and next experiment

Morrowind cadence fell on both renderers. Vulkan p95/p99 improved slightly, while OpenGL tails worsened. The small sample cannot establish causality or separate run variation, instrumentation effects, build differences, and patch interactions. PGR2 stayed near 30 FPS. The closing Vulkan baseline also improved its tails relative to the first baseline, so comparing candidates only with the first baseline exaggerates the apparent benefit.

Hold integration and retain these unfavorable observations. The user requested source/build comparison before considering further testing. No further runtime campaign is queued. The comparison below establishes what changed; it does not establish the cause of the cadence difference.

Power/utilization is not independently classified here: no new power conclusion was established. Any later comparison must account for delivered frames and equivalent workload.

## Build and source differences

The baseline `3caf5d85` and cumulative acceptance `208e4596` have identical Git trees. The candidate adds the following deltas to that tree:

| PR | Difference from baseline | Reach |
|---|---|---|
| #2 | Destroy owned SDL context and hidden window | Offscreen graphics resource teardown |
| #6 | Tested dirty-hint retirement and upload failure return | Vulkan texture cache validation |
| #10 | Whole-packet inline capacity preflight before mutation | Shared PFIFO/PGRAPH packet handling |
| #11 | Propagate download/upload failures; scoped map cleanup; checked invalidate/flush | Vulkan surface/texture preparation and consumers |
| #14 | Native GL S3TC upload; corrected border mip crop and shared face stride | OpenGL uploads and shared stored texture layout |
| #15 | Crop bordered compressed cubemap data to Vulkan copy extents | Vulkan cubemap uploads |
| #16 | Validate texture DMA class and target | Shared texture/palette source validation |
| #18 | Validate actual linear and packed-YUV row spans | Shared texture source bounds |
| #25 | Optional interruptible Windows wait and default-off menu setting | Host polling when enabled |

PRs #5/#7/#8 and the narrow fence behavior of #9 were already in cumulative S; they are not new runtime deltas here. The candidate has **no changes to PTIMER, APU, TCG/MMU, build.sh, configure, meson.build, or dependency manifests** relative to the baseline. Tests, reporting scripts, and documentation also differ but are not emulator hot-path changes.

### Recorded build comparison

| Property | Baseline | Candidate |
|---|---|---|
| Official build command | No `--debug`; Windows cross build | Same |
| Toolchain image digest | `09fdc183…3295e8b2` | Same |
| Meson buildtype / debug | `debug` / `true` | Same |
| Optimization | `2` | `2` |
| LTO | `true` | `true` |
| b_ndebug | `false` | `false` |
| x86_version | `3` | `3` |
| GLib | 2.83.2 | 2.83.2 |
| SDL | 3.4.10 | 3.4.10 |
| libepoxy | 1.5.10 | 1.5.10 |
| VMA | 3.4.0 | 3.4.0 |
| DSP | 0.1.3 | 0.1.3 |

The Meson `debug` label is present in both recorded builds despite optimization and LTO; it does not demonstrate that only the candidate was built with the emulator's `--debug` option. Both wrapper commands omit that option. DSP archive bytes match SHA-256 `ff031c6daf89f4c2c78a5943920bf483b044e8ec805feaf1efe32c80f991b09c`.

The candidate used a fresh checkout and a different internal build directory/LTO cache location. Those are reproducibility differences, not a demonstrated cause of the slowdown. Dependency manifests are unchanged; the table lists versions independently observed in both build logs, not a claim of complete dependency binary equivalence.

### Runtime configuration comparison

Parsed Morrowind Vulkan prelaunch TOMLs match except for each run's private HDD and EEPROM paths. Neither prelaunch nor saved post-exit config enables `cpu_saving_wait`; the candidate schema defaults it to false and startup transfers that setting to the poller. The high-resolution wait backend therefore was not selected by these saved settings. No claim is made that adding its disabled branch has mathematically zero overhead.

The wrapper's `PresentInterval=0` means *omit an override*, not interval zero. The underlying sealed result records effective interval 1. The performance report has been corrected accordingly. Morrowind's metric is a display-write cadence proxy, not displayed FPS.

These findings rule out an observed mismatch in the listed compiler settings and identify the actual source scope. They do not prove which change caused the unfavorable Morrowind result.

## Evidence

[Retail per-run measurements](retail-per-run.csv) · [XISO run receipts](xiso-runs.csv). These compact exports preserve the measured numbers; complete raw evidence remains retained with the completed worker campaign.

Repository issue: [#38](https://github.com/Mainkill1/xemu/issues/38). Current main and S have divergent history; a release must explicitly review the resulting tree rather than assuming an ordinary merge removes deferred Full-Speed changes. Draft #37 was excluded.

## Follow-up: Morrowind trace review and bounded repeat

[Offline trace reanalysis and separate repeat results](MORROWIND-TRACE-REVIEW.md) preserve the original table above. The Vulkan cadence flag repeated; OpenGL reversed direction. Release integration remains held.
