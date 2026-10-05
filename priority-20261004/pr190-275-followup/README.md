# #190/#275 shared sample-reader follow-up

Current source275 `ccf5c441`, fixture303 `37c72b07`, descriptor190 `8e996641`.
The first320 prototype runs are mixed and are retained separately. The current96 targeted runs pass all output/cursor checks. Both PRs remain HOLD. No native FPS improvement is established.

| Eight-worker VP case | #190 us | #275 us | Improvement | first/second balanced block |
| --- | ---: | ---: | ---: | ---: |
| mono-pcm | 1022427.5 | 1022522.5 | -0.01% | +0.19% / -1.40% |
| mono-adpcm | 950524.0 | 931198.5 | +2.03% | +1.76% / +2.30% |
| stereo-adpcm | 1032930.5 | 983071.0 | +4.83% | +5.87% / +4.81% |

The production fixture confirms one payload refill per callback at normal pitch (with extra refills for actual stereo page crossings). It does not provide the hypothetical hundred-block reuse. Reuse across32blocks is proven in a separate real-memory test.

Cold one-word mapping costs more than a generic read; two-word and larger cold mappings amortize on this host. The retained one-word mapping experiment is useful only after reuse is available. This is a host observation, not a universal hardware threshold.

The separate DeckPGR2 diagnostic sees94.06% descriptorhits and70.624millionphysicalpayloadreads (72.55%PCM). Inclusivecycle groups attribute0.256% to descriptor setup/teardown and2.077% to samplephysicalreads. Groups overlap; wholeprocesscounters and30secondtrace aredifferent runs. Profile finalization timeout remainsfailed/incomplete.

Every numeric run is retained in the CSV files. See manifest.json for source/timing scope, control lanes, failed negatives, limits and unchanged native gates. No gamepayloads,binaries,firmware,PCM,screenshots or rawtraces are published.

## Current cumulative stack versus main

72 additional balanced runs use the same v3 fixture and CPU0-8 affinity. Parent main `b4d69b24`, descriptor `8e996641`, stack `ccf5c441`. Eight-worker median VP-time reductions versus main: PCM8.65%, monoADPCM4.81%, changingstereoADPCM9.14%. Both balanced blocks have the same positive direction. The stack also passes all600 production resampler-state cases (8groups). These component measurements are not native FPS qualification. The unchanged three-title Windows/Deck game campaign has started; no merge has occurred.
