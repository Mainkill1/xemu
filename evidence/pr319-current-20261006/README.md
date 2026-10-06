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

16/16 real GL fixture checks pass (573 face/mip uploads, 7,386 existing texel checks), including exact bucket collisions, FIFO replacement, estimated-byte admission, upload-format readback/swizzle reset, native/fallback bordered DXT key-versus-allocation and active-stage/LRU teardown. The 2x storage test exercises real GL redefinition and the production invalidation seam; it does not replay a surface draw. SDL offscreen shared-context/reset checks pass; actual surface.c compiles with Werror. Checkpatch has zero errors/warnings. Native surface rendering, renderer switch, PGR2 streaming, current XISO before/after times, PCM and GPU memory high-water remain unqualified.

Raw game/process assets are absent. Benchmark source is repository-native diagnostic code and synthetic inputs only.
