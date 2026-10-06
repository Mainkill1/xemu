# PR #319 current component qualification

Candidate `84299f793b`, parent `741461cad9` (equivalent tree `15aa4bdd`). This is a local software-driver diagnostic. It does not establish a Steam Deck/Windows or game FPS result. Full native/XISO coverage remains pending; recommendation HOLD.

Actual generation/upload/release code is timed. Heavy validation stays outside the stopwatch. Every row is eight fresh-process attempts in ABBA/BAAB order; median of four values per side. Each attempt performs 8,192 changing-content uploads; the first 256 layouts are warmed at most. The 8,192-layout workload is a sequence of distinct width/height pairs. Pool logging is disabled for the timed comparison.

| Layouts | CPU A ms | CPU B ms | Improvement | ABBA | BAAB | Wall improvement | GL generations A / B |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 8.783 | 5.586 | +36.40% | +33.23% | +39.01% | +36.71% | 8192 / 0 |
| 8 | 8.216 | 5.755 | +29.95% | +31.96% | +24.78% | +29.76% | 8192 / 0 |
| 256 | 9.407 | 6.437 | +31.57% | +32.63% | +31.79% | +31.55% | 8192 / 0 |
| 257 | 8.705 | 9.791 | -12.48% | -10.35% | -13.14% | -12.42% | 8192 / 7936 |
| 8192 | 9.755 | 11.412 | -16.99% | -22.96% | -14.29% | -16.82% | 8192 / 7936 |

## Attribution and limits

A separate 65,536-operation 257-layout diagnostic enables lifetime counters. Its 65,793 lookups include warmup and untimed validation: 257 hits, 65,536 misses, 38,580 examined entries (0.59 per lookup), 65,280 FIFO evictions, 256-entry maximum and 17,104,896 estimated-byte maximum. It generates and deletes 65,536 GL names. The synthetic fixture does not execute same-stage replacement, so its zero outgoing-match count does not qualify that opportunity.

Lookup scanning is bounded and sparse in this trace, while name churn mostly remains in the oversubscribed case. That is mechanism evidence, not proof assigning the remaining CPU cost to one function. Retaining a name does not guarantee retained driver backing. Linux perf sampling was denied by kernel perf_event_paranoid=3; no sample-based hotspot claim is made. No system setting was changed.

Original repaired head `ded3ad6` records and the first uncommitted adaptive probe are retained separately. They are superseded diagnostic phases, not current-head qualification. They showed the same repeated-layout benefit/unique-layout cost tradeoff; no attempts were selected out.

16/16 real GL fixture checks pass (573 face/mip uploads, 7,386 existing texel checks), including exact bucket collisions, FIFO replacement, estimated-byte admission, upload-format readback/swizzle reset, native/fallback bordered DXT key-versus-allocation and active-stage/LRU teardown. The 2x storage test exercises real GL redefinition and the production invalidation seam; it does not replay a surface draw. SDL offscreen shared-context/reset checks pass; actual surface.c compiles with Werror. Checkpatch has zero errors/warnings. Native surface rendering, renderer switch, PGR2 streaming, current XISO before/after times and GPU memory high-water remain unqualified.

Raw game/process assets are absent. Benchmark source is repository-native diagnostic code and synthetic inputs only.

## Include repair and current-head applicability

Current head `66705284be` adds `qemu/osdep.h` first. The previous head failed real production compilation: the xxhash header was parsed before the QEMU base types/macros. The fixture included osdep first and masked this failure. Standalone `texture.c` now compiles warning-free with `-Werror`; checkpatch reports zero errors/warnings. Prior code with fixture-equivalent forced osdep and the repaired code produce identical 12,769-byte executable text sections in the maintained GCC production configuration. The timed algorithm has not changed.

## Churn isolation

These local diagnostics narrow the remaining question; they do not qualify native performance. Four arms run in balanced order, four processes per arm/workload. C uses the current generator but deletes on release; D does the same while retaining 256 other textures throughout the timed interval. Neither variant is production code.

| Layouts / operations | Main A CPU ms | Pool B CPU ms | Direct-delete C CPU ms | Fixed-retention D CPU ms |
|---|---:|---:|---:|---:|
| 257 / 65536 | 67.601 | 75.441 | 68.685 | 67.477 |
| 8192 / 8192 | 9.825 | 11.009 | 10.231 | 9.538 |
| 1 / 65536 | 61.718 | 42.263 | 62.501 | 62.544 |

Holding 256 textures fixed did not reproduce the pool's churn loss. Recycling changes both CPU bookkeeping and deletion order; these experiments do not uniquely assign the full difference to either one.

Direct production get/put operations with driver deletion replaced by a no-op take about 24 ns per operation at 1/8 layouts and 32 ns at 256/257 layouts (median of four 2-million-operation processes). This characterizes bookkeeping separately from driver work; it is not a complete texture-path cost.

A separate ABBA/BAAB diagnostic surrounds generation, deletion, bind, upload and swizzle calls with monotonic timers. For 257 layouts / 65,536 operations, median aggregate upload intervals are 48.626 ms on A and 49.073 ms on B; generation/deletion are 3.777/15.396 ms on A and 4.074/14.557 ms on B. Upload remains the largest instrumented interval. At one repeated layout, B eliminates generation/deletion and improves total measured CPU from 72.107 to 50.711 ms. These are perturbed runs: clock calls change execution cost and the churn loss magnitude, so they cannot replace the uninstrumented comparison or establish a precise causal accounting by summing medians.

The first call-profiler launch failed because unresolved epoxy entry points recursively dispatched into the diagnostic wrapper. Resolving each wrapped entry point before interposition fixes the harness. This is a diagnostic failure, not an xemu product crash; the failed attempt is not a successful timing sample.

Remaining work: native reuse/churn attribution, retained backing versus image redefinition, and matched affected XISO/game checks. No native procedure or settings were changed.

## Live logging repair and native diagnostic

Current head `0e3332f710` flushes optional owner-thread snapshots at most once per second, including a first generation before any release. A normal QMP quit need not run GL finalization; the first Deck diagnostic completed with exit0 but its counter artifact was empty. The test reproducing this flaw fails before the change and passes with it:17/17 real-GL checks,574 uploads,7386 existing texel checks. Logging errors close/disable the stream. Disabled logging performs no clock or file work. A snapshot is cumulative only through its timestamp; absence of a final row means later activity may be missing.

The native failure is retained in `native-deck-counter-failure.json`. Original saved inputs, waits, controller configuration, timeout and measurement boundaries were unchanged. It was a separate single OpenGL attribution diagnostic, not a paired qualification. Start/end image review found a course-camera intro at recording start and a stationary car at the end; it also fails the intended parked-scene boundary. No native performance uplift is established. Private images and guest/process data are excluded.

A dedicated logger-disabled comparison uses eight fresh processes in ABBA/BAAB per workload (same synthetic fixture):

| Layouts / operations | Before logger repair CPU ms | After CPU ms | Improvement | ABBA | BAAB |
|---|---:|---:|---:|---:|---:|
| 1 / 65536 | 41.394 | 41.613 | -0.53% | -1.67% | -0.48% |
| 257 / 65536 | 74.795 | 75.174 | -0.51% | +0.15% | -0.51% |
| 8192 / 8192 | 11.153 | 11.804 | -5.83% | -5.71% | -6.43% |

The unique-layout slowdown remains unresolved. Both branches skip clock/file operations with logging disabled; this result is not evidence assigning the difference to timer overhead. Original reuse/churn comparisons and all short attempts remain available, including the earlier release-only diagnostic. CI on667 completed44/44; exact current-head CI is tracked in the PR body.
