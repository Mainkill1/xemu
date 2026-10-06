# PR #200 current compiled-head qualification

Reference product main `741461cad9bdaf3d34cadc98a5879ce48057de33`, tree `663d7a97a1e33b0ffbec00c15d3bee8f3514d917`; equivalent release build `42bd5e11e790`. Candidate `197654b3c09d1b7530689a23d24de9e849fedb9f` changes Auto to at most four workers for default sinc and preserves explicit 1–16 overrides. Exact-head CI is 44/44 green; production fixture 25/25 and independent source review pass. No UI layout benchmark was added.

## Current native results

48 runs, ABBA followed by BAAB for each of three games on each host. Unchanged saved procedures, inputs, waits, configurations and final-input boundaries. All 96 recording endpoints reviewed. The statistic is the median of four canonical per-run values per side; positive improvement is better. CPU is summed xemu process usage, one logical core = 100%. Guest cadence is READ_3D progress, not SDL-presented FPS. Frame times are milliseconds from canonical analysis; the configured 30-second measurement uses its original 25-second frame-analysis tail.

| Cohort | CPU A → B | CPU reduction | Guest cadence A → B | Avg ms A → B | p95 ms A → B | p99 ms A → B | Max ms A → B |
|---|---:|---:|---:|---:|---:|---:|---:|
| Deck Morrowind | 225.49 → 208.30% | +7.63% | 12.87 → 13.24 | 74.77 → 73.25 | 100.00 → 99.98 | 116.66 → 100.95 | 149.95 → 120.02 |
| Windows Morrowind, cache-ineligible diagnostic | 284.06 → 231.65% | +18.45% | 32.53 → 33.18 | 30.61 → 30.09 | 33.73 → 33.72 | 50.03 → 50.03 | 50.37 → 50.39 |
| Windows Conker | 247.54 → 211.10% | +14.72% | 30.00 → 30.00 | 33.33 → 33.33 | 33.50 → 33.57 | 33.72 → 33.76 | 34.07 → 34.24 |
| Deck Conker | Withheld: wrong scene | — | — | — | — | — | — |
| Deck PGR2 | Withheld: startup screens | — | — | — | — | — | — |
| Windows PGR2 | Withheld: unconfirmed start | — | — | — | — | — | — |

Deck Morrowind CPU improvements are +6.89% / +7.78% by block, cadence +2.50% / +2.67%, mean frame time +1.65% / +2.00%, p99 +4.88% / +13.46%, maximum +15.61% / +13.19%. Windows Morrowind diagnostic CPU improves +16.76% / +17.57%; maximum-frame directions disagree (-16.09% / +0.14%). Windows Conker CPU improves +15.19% / +13.85%; all aggregate frame changes remain below 1%. Scene/animation progress and spike causes are uncontrolled: tails are observed differences, without causal worker-count attribution.

## Retained scene limitations

The runner reported all these attempts completed/PASS/complete, and all except Windows Morrowind comparison-eligible. Manual review retains stricter limits separately:

- Deck Conker reference cell 7, `20261006-053746940-efc23d44dc9c410e8d9d55a09fd7775c`, shows a multiplayer match instead of the expected Xbox Live & Co menu at both endpoints. Withhold the entire cohort.
- Deck PGR2 reference cell 6, `20261006-060242069-2df8a0d718084cb3ae2b466c757b4a81`, shows Microsoft Game Studios/Bizarre Creations startup screens instead of the race during measurement. Withhold the entire cohort.
- Windows PGR2 candidate cell 5, `20261006-055924912-a6d74984bfc74928a796a0f345e0d125`, has a white starting image and the expected parked car at the end. Starting scene is unconfirmed; withhold the entire cohort.
- Other PGR2 pairs show the same Hong Kong car, ending parked at 0 mph. Original Deck flyover versus Windows countdown starts remain different; no input or delay was changed.

No failed cell is replaced. All 48 original canonical numeric rows remain in `per-run.csv`/`final-current-per-run.json`. Manual flags do not rewrite automated outcomes. Conker receipts for the failed reference and adjacent controls show two approximately 100 ms A presses, neutral release, no provider error and complete cleanup; OS submission does not prove guest consumption. The scene failure cause remains unresolved.

## Scope and remaining qualification

Both rigs: Vulkan, Auto/default sinc. Deck AMD Custom APU 0405/RADV Vangogh, 8 logical cores; Windows Ryzen 9 6900HX/RTX 3070 Ti Laptop, 16 logical cores. Hardware and immutable application/procedure/config identities are included. Reference and candidate configurations are byte-identical within each host/game, with no gameplay edits.

The shared XISO catalog contains 171 leaves, 149 oracles and 22 missing. Four full paired requests were rejected at the existing oracle-coverage gate. Frozen matching CPU controls completed 32/32 attempts (64 leaf records), all correctness PASS. Deck admission is 16/16; Windows 0/16 due to cache. [All per-leaf timings, order directions, >1% changes and official reports](xiso/controls.md) are retained. Deck GL direct-loop mean worsens 1.26% with opposite block directions; VK improves 1.36% with opposite directions. Windows diagnostic mean changes are -1.11%/-1.77% on GL and -0.63%/-1.74% on VK. No speedup or full-suite qualification follows. They are unaffected controls; the registered catalog contains no affected audio leaf. There is no new OpenGL title, PCM/listening or active save/load qualification.

Recommendation remains **HOLD** for the disclosed scene/cache/oracle and audio-latency limits. Previous same-binary and upstream-only worker studies remain separate, unpooled evidence. Raw screenshots, logs, process/game/resource bytes, ISOs, ROMs, firmware and private host paths are retained privately, outside Git and main.
