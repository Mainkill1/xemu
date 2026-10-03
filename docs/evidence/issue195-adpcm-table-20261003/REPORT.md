# PR #195 Steam Deck qualification

Date: 2026-10-03  
Runner: Steam Deck `10.0.0.123`  
Metric: fixed-work elapsed time; lower is better

## Compared builds

- **A / baseline:** main `76c23c7d444a6f12c9778bb2c35fab513f6c8056`
- **B / candidate:** PR #195 `6cca0fe8604f0253ba8f779ff3ff560cf366fa4a`
- Both executables were built successively in the same build tree with the same compiler, flags, fixture source, and pinned runtime libraries.
- Workload: production VP callback, fetch, resampler reads, worker dispatch, and mix for 45 looping voices, 20,000 frames, eight workers, and a 256-frame warmup.
- Each workload ran two fresh A/A controls followed by ABBA and BAAB blocks.

## Results

| Workload | Fresh A/A (s) | A/A spread | A mean (s) | B mean (s) | Improvement | ABBA | BAAB |
|---|---:|---:|---:|---:|---:|---:|---:|
| Mono ADPCM, affected | 6.179 / 6.154 | 0.42% | 6.184 | 6.196 | **-0.19%** | +0.34% | -0.71% |
| Stereo ADPCM, affected | 6.970 / 6.867 | 1.48% | 6.959 | 6.874 | **+1.22%** | +0.89% | +1.55% |
| Mono PCM, unaffected control | 7.261 / 7.203 | 0.81% | 7.181 | 7.011 | **+2.37%** | +2.63% | +2.10% |

Positive percentages mean that B completed the fixed work faster. Every matched run produced the fixed checksum `3686400000`, reported `correctness: PASS`, and satisfied the runner evidence checks: **30/30 passed**.

## Interpretation

The candidate does not show a repeatable ADPCM-specific gain. Mono ADPCM is neutral to slightly slower and its ABBA/BAAB estimates disagree. Stereo ADPCM improves modestly, but the unaffected PCM control improves more than either affected workload. This pattern cannot attribute the faster observations to the precomputed ADPCM transitions.

The recommendation remains **HOLD**. The implementation and equivalence tests are reviewable, but these measurements do not justify merging it as a performance change.

## Excluded campaign

An earlier 30-run campaign compared the candidate with an older prebuilt main fixture. Its unaffected PCM control improved by 2.20%, exposing unmatched build provenance. Those runs are preserved in the evidence archive but excluded from the result table. The matched campaign rebuilt A and B successively in one build tree before upload.

## Evidence contents

`EVIDENCE.tar.gz` contains immutable runner definitions, physical run order, per-run stdout, runner assessments, input manifests, extracted measurements, and both matched and excluded campaign summaries. `INDEX.json` records archive and executable hashes.
