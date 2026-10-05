# Compiler inlining comparison

#275 current stack `ccf5c441af26e49be4ddf9f3d7d2b1e7dbc78531`, compared with a private variant forcing only `voice_get_samples()` inline. No production source was changed or pushed.

## Method

GCC 14.2.0, identical compiler settings and shared v3 throughput fixture object from #303 `37c72b07783f810076fb6961756380c28942a4d2`. Both executables were relinked against that same object and identical remaining link arguments. Each of six cases ran ABBA then BAAB: 20,000 fixed VP frames per run, 256 warmup frames, 45 independently routed voices, sinc converter, CPU affinity 0–8. Full output and cursor validation stays outside the VP stopwatch. These are component timings on the Linux build host, not native game FPS.

All **48/48** measured runs passed output/state checks. Both newly linked variants independently passed **24/24** production fixture cases.

## Results

Positive percentages mean less VP execution time. Medians use four separate runs per variant; ABBA/BAAB blocks are retained separately.

| Workers | Payload | Current median µs | Inline median µs | Time reduction | ABBA | BAAB |
|---:|---|---:|---:|---:|---:|---:|

| 1 | mono-pcm | 3,158,204.0 | 3,174,632.0 | -0.52% | -0.66% | -0.72% |
| 1 | mono-adpcm | 2,735,766.5 | 2,737,185.5 | -0.05% | +1.01% | -0.44% |
| 1 | stereo-adpcm | 2,954,954.5 | 2,941,408.0 | +0.46% | +0.98% | -0.44% |
| 8 | mono-pcm | 1,013,964.0 | 1,027,874.5 | -1.37% | -1.65% | -0.91% |
| 8 | mono-adpcm | 930,978.5 | 932,740.0 | -0.19% | +0.01% | -0.29% |
| 8 | stereo-adpcm | 987,926.5 | 977,222.5 | +1.08% | -2.58% | +0.99% |

## Decision

**Do not add forced inlining to #275.** PCM is slower in both orders with either worker count, including **1.37% more median time** with eight workers. The eight-worker changing-stereo ADPCM median improves 1.08%, but its ABBA block regresses 2.58% and BAAB improves 0.99%; that is not a consistent improvement. The other changes are small and often disagree by order. No further compiler tuning is required for this experiment.
The disassembly confirms that the separate `voice_get_samples()` symbol disappeared and its body entered `voice_resample_callback()`. VP object text grew from 28,325 to 28,669 bytes (+344). This confirms the intended compiler change, but does not establish a beneficial game-performance effect or explain the Morrowind tails.

## Superseded preliminary attempt

The initial attempt used an older executable with 16 registered fixture cases versus a newer inline executable with 24. Final verification caught that mismatch. Those preliminary percentages are withdrawn: both variants were relinked with the exact same fixture and the complete 48-run experiment was repeated. Only the corrected comparison is published here.

## Scope

The production #190/#275 heads remain unchanged. Native game regressions and incomplete comparisons are recorded separately; these passing fixture checks do not waive them. Evidence is retained on a dedicated branch outside `main`.
