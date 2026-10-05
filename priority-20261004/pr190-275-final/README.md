# Final #190 / #275 qualification

Candidate: `7dcdcb0dcdfae81ddd0795b0782e63aa0ea3caa3`, stacked on #190 `8e9966415808f51feae2c566015b89da689a67d2`.
Baseline: main `b4d69b2440a33ad93987b269c2ecac97c45367bc`.

Current-head CI: 42/42 successful checks; #190: 40/40.
Fresh production validation: 24/24 VP cases and 600 resampler-state cases.
The private additional real-memory fixture poisons all callback caches except ownership markers, adds empty-cache and descriptor-only cleanup, and passes 20/20 ASan/UBSan cases. Leak scanning disabled because sandbox ptrace prevents LeakSanitizer. This is not a LeakSanitizer pass.

The fixed-work VP experiment uses identical newly linked #303 v3 fixture objects, 45 independently routed voices, 8 workers, 20,000 frames per run and CPU affinity 0–8. Output/state checks remain outside the production-VP timer. ABBA then BAAB produces 24/24 checked measurements.

| Case | VP time reduction vs main | ABBA | BAAB |
|---|---:|---:|---:|
| PCM | 8.26% | 11.58% | 7.58% |
| Constant mono ADPCM | 1.68% | -0.66% | 3.34% |
| Changing encoded stereo ADPCM | 9.06% | 10.10% | 5.87% |

Mono ADPCM order directions disagree, so its small aggregate gain is inconclusive. These are component results on the build host, not native game FPS. Raw timings, output/state outcomes, ranges and both order blocks are retained.

Final native Windows/Steam Deck campaigns are running with exact push-CI artifacts and unchanged frozen game procedures. No final native gain or merge is claimed here yet. This branch is an orphan numerical evidence archive and will not be merged into product main. Game images, resources, PCM and private traces are excluded.
