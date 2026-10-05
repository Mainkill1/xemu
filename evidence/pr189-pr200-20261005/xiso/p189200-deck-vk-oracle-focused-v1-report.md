## XISO campaign p189200-deck-vk-oracle-focused-v1 — inconclusive

Suite `pr256-deck-opengl-v5` · ISO `0a98e667d56b16dd91f028ef09f9e535199c006ca3800b5b54238799a4436373` · catalog `sha256:f8636cdd127be157dc1c55742c32e3ffc907cb4f16e812e0bfc342b83b7660bd` · suite qualification `unverified`
Indexed attempts: 8/8. Changes above 1%: 3 metric rows. Opposite ABBA/BAAB outcomes: 4.
Improvement is positive when the candidate is faster. Each order uses two A and two B attempts; the combined value uses four of each. These are guest timings, not whole-game FPS. Attempt eligibility does not qualify a candidate suite as an approved baseline.

### Category summary
| Category | Leaves | >1% faster | >1% slower | Median leaf change |
|---|---:|---:|---:|---:|
| cpu | 2 | 1 | 0 | +0.96% |
Category median is the median of leaf percentage changes; it is not an additive runtime or GPU cost.

### Per-leaf results
| Category | Test | A median (µs) | B median (µs) | Improvement | ABBA | BAAB | Changes above 1% |
|---|---|---:|---:|---:|---:|---:|---|
| cpu | cpu_floating_point.sse_scalar | 1215203.00 | 1202272.00 | +1.06% | +1.44% | -0.03% | mean_us +1.07%, median_us +1.06%, min_us +1.14% (orders disagree) |
| cpu | cpu_floating_point.x87_scalar | 1125763.75 | 1116101.25 | +0.86% | -0.20% | +0.86% | — (orders disagree) |

The full CSV includes each leaf's mean, median, min, max and p95 guest metrics, plus the four-attempt A/B distributions. A changed pixel or guest timing needs separate correctness and gameplay review.
