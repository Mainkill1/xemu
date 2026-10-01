# Native Deck pilots — NOT COMPARABLE

These three short PGR2 Vulkan pilots qualify the runner deployment and expose a procedure problem. **They establish no game speedup or slowdown from the multiply change.** All three completed their input plans, but none met the declared requirement of 160 positive frame intervals in the final 25 seconds. The C runs also failed image brightness checks. A/B are single attempts, not a balanced campaign.

| Attempt | Product | DSP setting | CPU mean (core %) | Reported flip cadence (frames/s) | Correctness | Evidence | Comparison |
|---|---|---|---:|---:|---|---|---|
| C parent | `ee5ce48b4878` | Full DSP, C | 200.842 | 1.574 | Failed | Incomplete | Ineligible |
| C candidate | `b9359dbf55c7` | Full DSP, C | 190.145 | 1.283 | Failed | Incomplete | Ineligible |
| JIT parent control | `ee5ce48b4878` | Full DSP, JIT | 243.194 | 0.421 | Passed | Incomplete | Ineligible |

The CPU values are runner process core-percent averages in the `in-game-throttle` host segment. Flip cadence is the runner's weighted last-five guest-flip records; its guest clock/window is separate from the host segment. These diagnostic rows are not aligned fixed-work comparisons. Frame-tail statistics are absent; percentages and confidence intervals are therefore N/A. Screenshots show the race countdown or its immediate completion, rather than a validated steady racing workload. Do not promote the observed CPU reduction or cadence difference to a patch effect.

## Exact procedure and artifacts

The native parent/candidate packages come from accepted-main CI `36839366349` and candidate PR CI `36881642099`. `IDENTITY.json` pins executable hashes and exact source. Both Deck packages use the same parent runtime-library bundle: 37 of 38 original libraries match, but the two CI libglib files differ, so the candidate package's libglib is deliberately excluded. Compiler/options matching has not yet been independently qualified; these pilots are not a completed toolchain-controlled comparison.

Runner: `0.2.0+0ba533ed89c8bb3cf530c4bfff8dbb190b3f08be`, instance `ab50c5cb96884152998523afb762d683`, Steam Deck/SteamOS, Mesa 25.3 RADV Vangogh, Vulkan, vsync off, VP workers 0, full DSP enabled. The private EEPROM, HDD and firmware/disc identities are in the frozen manifests. Cache mode is **cold**, unlike the issue's still-required warm-scene performance qualification.

Reusable frozen definitions are retained under `frozen-procedures/`:

- C: `issue229-deck-pgr2-full-dsp-c-v1`, revision `6944074de8e224d19c16ebdb6f2ceec05e6342e6941fc3595a81efd81f28ba06`.
- JIT: `issue229-deck-pgr2-full-dsp-jit-v1`, revision `65a2c44d2b5e1f3f3eb07f386f6b5b4639a40a84003c79307522b5b22bd089aa`.

`SUMMARY.json` retains all run IDs and exact canonical failures. Each attempt directory contains `assessment.json`, `performance.json`, source metrics/guest logs, segment markers, screenshots, effective configuration and the complete storage ledger. `prepare-native.py` retains the preparation recipe as executed; it uses this workspace's paths and is not a portable test-runner replacement. Its Windows v1 attempt is an unfinished preparation, not a validated reusable Windows test.

Parent stderr also records a GLib `g_source_destroy` assertion at shutdown. This occurs on the unmodified parent and remains unclassified; it is not assigned to the candidate.

## Mesa storage proof

The schema-2 storage gate passes on all three runs without `AllowUncontrolledDriverCache`. The C runs each begin with zero driver-cache files/bytes and end with 795 files / 3,797,539 bytes; the JIT control ends with 818 / 3,839,741 bytes. The complete ledgers independently record `DriverNamespaceVerified=true`, `DriverQualification=mesa_private_disk_writes_observed`, `TargetStopped=true`, and no issues. **Storage `ComparisonReady=true` is not overall run eligibility.** Driver RAM and the OS page cache remain explicitly uncontrolled.

The separate operator workflow upgraded the Deck while idle to runner draft PR #80's binary, preserving the previous runner/service, existing configuration and evidence. The configuration hash is `8d0265ba728e1f01100a9c579ef9c888abdb5053a12b7277ba7f3eba34b0bd60`; deployment records are included here. No runner evidence-storage commit or merge was made.

## Next qualification step

Preserve v1 failures. Freeze a distinct procedure that reaches and verifies a stable scene, includes a sufficiently long bounded observation window at the observed full-DSP throughput, and checks the game viewport separately from window chrome. Retain sample gates and the original image threshold. Qualify matching build options and controlled warm-cache state, then run A/A plus ABBA and BAAB for ordinary and reduced-work C settings, with JIT controls. APU/DSP profiling and PCM parity remain separate requirements. No v1 result is retroactively reclassified.

## Windows operator recovery

The independent [recovery ledger](windows-recovery/recovery-report.json) records the prior disk-full preparation failure and the verified deployment of the same runner revision on Windows. It keeps queues, saved definitions and historical results on C:, and uses D: for runtime copies/catalog. All 30 copied files, 11,377,843,780 bytes, match their originals and all 15 assets match manifest pins. One interactive runner owns the HTTP listener; no Windows game was launched. C: packages/results still consume C: capacity, and the free-space gate remains unchanged. The prior failed draft/operation remains failed. Future payloads should use catalog asset `pgr2-freshboot-base-v1` rather than duplicate its 2.6 GB HDD in each package.
