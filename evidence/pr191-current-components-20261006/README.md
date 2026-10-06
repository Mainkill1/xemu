# PR #191 — current-main component qualification

Parent `afdde9eb62accd07686be104ebf4a9381785aaab`; candidate `30f7e6b627d391b945ffb35234454aba2dcdfa83`. This is an authoring-host component comparison, not a native emulator/game campaign. The evidence branch is excluded from main.

Actual current VP/APU/debug/monitor sources and the unchanged main v3 production fixture were recompiled for each side. Generated configuration and other emulator support objects were reused. Baseline APU sources/headers were checked against exact main. The format-only diff preserves the decoded-block cache, descriptor/payload mappings, worker completion predicate and all dynamic cursor/state access sites.

All 64 attempts pass output/cursor/actual-worker-distribution checks. Timing contains VP work and per-frame clock reads; full output validation is outside the measured interval. Positive improvement means less elapsed time. Host scheduling/power/cache conditions are uncontrolled.

| Profile | Workers | ABBA A/B µs | Improvement | BAAB A/B µs | Improvement |
|---|---:|---:|---:|---:|---:|
| mono-adpcm stable | 1 | 687791.0/654661.0 | +4.82% | 673971.5/646093.5 | +4.14% |
| mono-adpcm changing | 1 | 695622.0/681954.0 | +1.96% | 702898.0/677300.0 | +3.64% |
| stereo-adpcm changing | 1 | 738492.0/705451.5 | +4.47% | 735514.0/712955.0 | +3.07% |
| mono-pcm stable | 1 | 777185.0/752016.0 | +3.24% | 787314.0/757299.0 | +3.81% |
| mono-adpcm stable | 8 | 230542.5/223165.5 | +3.20% | 230783.5/225013.5 | +2.50% |
| mono-adpcm changing | 8 | 234779.0/229590.0 | +2.21% | 267207.5/245325.0 | +8.19% |
| stereo-adpcm changing | 8 | 244029.5/236269.0 | +3.18% | 244370.5/237960.5 | +2.62% |
| mono-pcm stable | 8 | 253566.0/248916.0 | +1.83% | 254081.5/246162.0 | +3.12% |

Decoder: GCC/Clang normal and UBSan each pass five checks (720,896 word combinations each). Parent/candidate each pass 24 VP cases, three PIO/direct-RAM/table-base boundary checks, and 18 sample-memory checks. All 600 finite-source characterization records match in every field. The wrapper entrypoint declaration is required by the strict missing-prototype compiler gate; checkpatch flags it as a known test-harness exception.

Historical native/XISO measurements belong to the previous parent/head and remain linked in the PR. Current-head CI/native/XISO acceptance remains pending; these component results do not establish game FPS, CPU or tail improvements.
