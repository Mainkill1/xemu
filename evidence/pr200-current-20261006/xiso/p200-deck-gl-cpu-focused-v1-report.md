## XISO campaign p200-deck-gl-cpu-focused-v1 — inconclusive

Suite `pr256-deck-opengl-v5` · ISO `0a98e667d56b16dd91f028ef09f9e535199c006ca3800b5b54238799a4436373` · catalog `sha256:f8636cdd127be157dc1c55742c32e3ffc907cb4f16e812e0bfc342b83b7660bd` · suite qualification `unverified`
Indexed attempts: 8/8. Changes above 1%: 2 metric rows. Opposite ABBA/BAAB outcomes: 1.
Improvement is positive when the candidate is faster. Each order uses two A and two B attempts; the combined value uses four of each. These are guest timings, not whole-game FPS. Attempt eligibility does not qualify a candidate suite as an approved baseline.

### Category summary
| Category | Leaves | >1% faster | >1% slower | Median leaf change |
|---|---:|---:|---:|---:|
| cpu | 2 | 0 | 0 | -0.44% |
Category median is the median of leaf percentage changes; it is not an additive runtime or GPU cost.

### Per-leaf results
| Category | Test | A median (µs) | B median (µs) | Improvement | ABBA | BAAB | Changes above 1% |
|---|---|---:|---:|---:|---:|---:|---|
| cpu | cpu_floating_point.x87_scalar | 1085298.00 | 1087771.25 | -0.23% | -0.63% | +0.29% | — |
| cpu | cpu_translation_blocks.direct_loop | 23402.00 | 23552.50 | -0.64% | +0.21% | -1.22% | mean_us -1.26%, min_us +3.16% (orders disagree) |

The full CSV includes each leaf's mean, median, min, max and p95 guest metrics, plus the four-attempt A/B distributions. A changed pixel or guest timing needs separate correctness and gameplay review.
