# Morrowind: review of the existing comparison

This is an offline reanalysis of the original four cells. Tested baseline
`3caf5d85423298c62344f8fe44fe9a1176b8ae81` (tree-equivalent to frozen S)
and candidate `57cf3f0a4b443e8074c79710139d2b8aaf6ac0aa` are unchanged.
It does not qualify later release or timer/UI changes.

All eight start/end screenshots were inspected: they show the expected gameplay
scene without a pause or controller-reconnect overlay. This closes the earlier
image-review gap for these captures. Screenshots are not continuous video and
cannot establish equal host load. Final image hashes match the original receipts.

The archived display-write traces exactly reproduce the published cadence and
nearest-rank p95/p99. No samples or outliers were removed.

| Renderer | Build | Cadence proxy | Mean interval ms | Median ms | p95 ms | p99 ms | Maximum ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Vulkan | Baseline | 28.545524 | 35.031761 | 34.155 | 44.220 | 51.314 | 76.921 |
| Vulkan | Candidate | 26.792926 | 37.323285 | 36.586 | 44.188 | 50.392 | 75.585 |
| OpenGL | Baseline | 38.768135 | 25.794380 | 25.073 | 31.761 | 36.907 | 43.339 |
| OpenGL | Candidate | 37.640221 | 26.567325 | 25.714 | 33.902 | 38.080 | 41.561 |

There is no display-write interval above 100 ms in any measured window. The
candidate's Vulkan median is higher even though its p95/p99 are slightly lower.
The original flag is therefore a shift in typical cadence, not an average
penalty caused only by a long isolated stall. These traces do not prove that
all rendering or device threads remained responsive between events.

The one-second Vulkan event counts are 25–33 for baseline and 25–29 for
candidate. This within-run variation is another reason not to treat thousands
of correlated events as independent trials. One run per build/renderer cannot
identify a source regression or establish non-inferiority.

## Source inspection and next discriminating measurement

The shared linear-span and DMA checks affect both renderers. The additional
stored-mip length walk applies only to cubemaps; it is not an added all-texture
full-mip scan. Texture shapes remain zero-initialized before hashing, so this
inspection does not establish uninitialized key padding. Packet-capacity checks
are another shared change. GL native upload changes and Vulkan readback/upload
failure handling affect their respective backends. The recordings contain no
per-function costs or texture-shape counts that select one of these as a cause.
Do not remove bounds or lifecycle repairs to chase this timing difference.

## Completed bounded repeat

The unchanged four-cell queue completed with the same original executables,
private snapshot disks, five-second Start/B route, and 10-second windows. All
four final images show gameplay without pause/reconnect overlays; all private
disks were deleted, seed identities unchanged, and no emulator remained.

| Renderer | Baseline cadence | Candidate cadence | Observed direction | Baseline p95/p99 ms | Candidate p95/p99 ms |
| --- | ---: | ---: | --- | --- | --- |
| Vulkan | 28.782280 | 27.729001 | -3.66% | 39.600 / 46.659 | 43.263 / 49.622 |
| OpenGL | 34.196540 | 37.059454 | +8.37% | 37.613 / 42.297 | 34.566 / 37.635 |

[Repeat CSV](morrowind-repeat.csv) · [manifest](morrowind-repeat-manifest.json).
No new build, XISO run, or PGR2 run was performed. These are display-write cadence
proxies, not rendered FPS or an established improvement/regression decision.

Vulkan's unfavorable mean-cadence direction repeats (-6.14%, then -3.66%).
OpenGL reverses direction (-2.91%, then +8.37%); this does not support a
consistent slowdown across both renderers. Both pairs used baseline-then-candidate
ordering, so order/host-state effects remain confounded. Do not pool event
percentiles or infer confidence from the number of events. Integration remains
held. The next investigation should attribute the actual Vulkan surface/texture
preparation deltas against the same baseline, not restart the timer redesign or
repeat the entire qualification suite without a discriminating question.

## Reproduction

Run `python3 analyze-morrowind.py` in this directory. The standalone offline
analyzer checks regenerated cadence/p95/p99 against the archived manifest and
prints the summary CSV. It does not run the emulator or modify test scripts.

- [Relative event timestamps](morrowind-event-times.csv)
- [Generated summary](morrowind-trace-summary.csv)
- [Source, image, trace, and runner identities](morrowind-review-manifest.json)
- [Original qualification report](REPORT.md)
