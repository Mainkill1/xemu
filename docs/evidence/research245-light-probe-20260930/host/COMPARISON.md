# Full host comparison

Every row is process CPU time for a fixed-work standalone collector loop. Four retained runs per setting; all operation-count checks pass. Improvement = 100 × (reference − candidate) / reference. No guest speedup claim.

| Workload / comparison | Reference median [min–max] ms | Candidate median [min–max] ms | Delta ms | Improvement % |
| --- | ---: | ---: | ---: | ---: |
| lookup-hooks / legacy-all-vs-lighter-all | 47.431 [47.127–47.721] | 10.751 [10.680–10.894] | -36.680 | +77.33% |
| lookup-hooks / legacy-off-vs-current-off | 8.350 [8.340–8.364] | 9.390 [9.365–9.405] | +1.040 | -12.46% |
| lookup-hooks / same-executable-off-vs-counters | 9.404 [9.372–9.430] | 10.442 [10.411–10.457] | +1.038 | -11.04% |
| lookup-hooks / same-executable-counters-vs-occupancy | 10.414 [10.384–10.433] | 10.402 [10.387–10.418] | -0.012 | +0.11% |
| lookup-hooks / same-executable-counters-vs-timing | 10.387 [10.378–10.401] | 10.572 [10.551–10.581] | +0.185 | -1.78% |
| empty-cache-clears / legacy-all-vs-lighter-all | 60.667 [60.613–60.726] | 43.219 [43.149–43.331] | -17.448 | +28.76% |
| empty-cache-clears / legacy-off-vs-current-off | 42.866 [42.840–42.888] | 42.368 [42.340–42.381] | -0.497 | +1.16% |
| empty-cache-clears / same-executable-off-vs-counters | 42.358 [42.337–42.371] | 42.394 [42.386–42.408] | +0.037 | -0.09% |
| empty-cache-clears / same-executable-counters-vs-occupancy | 42.381 [42.359–42.404] | 43.075 [43.070–43.078] | +0.694 | -1.64% |
| empty-cache-clears / same-executable-counters-vs-timing | 42.357 [42.353–42.367] | 43.009 [42.996–43.031] | +0.653 | -1.54% |

[Every retained/warmup observation](runs.json), [A/A observations](aa-runs.json), [A/A summary](aa-summary.json), [Executed commands/source hashes](receipt.json), [Configuration reproduction](configuration-equivalence.json).

NULL-probe lookup calls differ from real CPU dispatch, which skips both hooks when disabled. Empty-clear tests do not represent nonempty occupancy or real guest invalidation. Small mode differences require further repetitions; host frequency and thermal state were not fixed.
