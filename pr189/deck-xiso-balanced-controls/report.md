## XISO campaign pr189-008-deck-balanced-controls-v1 — inconclusive

Suite `pr256-deck-opengl-v5` · ISO `0a98e667d56b16dd91f028ef09f9e535199c006ca3800b5b54238799a4436373` · catalog `sha256:f8636cdd127be157dc1c55742c32e3ffc907cb4f16e812e0bfc342b83b7660bd` · suite qualification `unverified`
Indexed attempts: 16/16. Changes above 1%: 9 metric rows. Opposite ABBA/BAAB outcomes: 1.
Improvement is positive when the candidate is faster. Each order uses two A and two B attempts; the combined value uses four of each. These are guest timings, not whole-game FPS. Attempt eligibility does not qualify a candidate suite as an approved baseline.

### Category summary
| Category | Leaves | >1% faster | >1% slower | Median leaf change |
|---|---:|---:|---:|---:|
| cpu | 1 | 1 | 0 | +1.81% |
| shaders | 1 | 1 | 0 | +4.06% |
Category median is the median of leaf percentage changes; it is not an additive runtime or GPU cost.

### Per-leaf results
| Category | Test | A median (µs) | B median (µs) | Improvement | ABBA | BAAB | Changes above 1% |
|---|---|---:|---:|---:|---:|---:|---|
| cpu | cpu_floating_point.sse_scalar | 1195661.75 | 1174026.75 | +1.81% | +1.57% | +1.89% | max_us +2.42%, mean_us +1.41%, median_us +1.81%, min_us +1.11%, p95_us +2.42% |
| shaders | pipeline_texture_switch.texture_switch | 56564.25 | 54266.00 | +4.06% | +1.41% | +7.00% | max_us +4.41%, mean_us +3.49%, median_us +4.06%, p95_us +4.41% (orders disagree) |

The full CSV includes each leaf's mean, median, min, max and p95 guest metrics, plus the four-attempt A/B distributions. A changed pixel or guest timing needs separate correctness and gameplay review.
