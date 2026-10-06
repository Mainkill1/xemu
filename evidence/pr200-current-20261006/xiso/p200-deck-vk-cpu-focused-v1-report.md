## XISO campaign p200-deck-vk-cpu-focused-v1 — inconclusive

Suite `pr256-deck-opengl-v5` · ISO `0a98e667d56b16dd91f028ef09f9e535199c006ca3800b5b54238799a4436373` · catalog `sha256:f8636cdd127be157dc1c55742c32e3ffc907cb4f16e812e0bfc342b83b7660bd` · suite qualification `unverified`
Indexed attempts: 8/8. Changes above 1%: 5 metric rows. Opposite ABBA/BAAB outcomes: 4.
Improvement is positive when the candidate is faster. Each order uses two A and two B attempts; the combined value uses four of each. These are guest timings, not whole-game FPS. Attempt eligibility does not qualify a candidate suite as an approved baseline.

### Category summary
| Category | Leaves | >1% faster | >1% slower | Median leaf change |
|---|---:|---:|---:|---:|
| cpu | 2 | 1 | 0 | +0.31% |
Category median is the median of leaf percentage changes; it is not an additive runtime or GPU cost.

### Per-leaf results
| Category | Test | A median (µs) | B median (µs) | Improvement | ABBA | BAAB | Changes above 1% |
|---|---|---:|---:|---:|---:|---:|---|
| cpu | cpu_floating_point.x87_scalar | 1079017.00 | 1084962.00 | -0.55% | +0.26% | -0.44% | max_us +1.30%, p95_us +1.30% (orders disagree) |
| cpu | cpu_translation_blocks.direct_loop | 22982.00 | 22714.00 | +1.17% | +0.41% | +1.17% | mean_us +1.36%, median_us +1.17%, min_us -2.40% (orders disagree) |

The full CSV includes each leaf's mean, median, min, max and p95 guest metrics, plus the four-attempt A/B distributions. A changed pixel or guest timing needs separate correctness and gameplay review.
