# PR275: Steam Deck parent qualification

## Decision: HOLD — no candidate performance result

All runs used Steam Deck `10.0.0.123`, Vulkan, full DSP JIT, 128 MiB guest,
private HDD/EEPROM, private cold Mesa/application cache, and pinned parent
runtime libraries. Execution used the maintained HTTP client. No Windows
native test, cache purge, waiver, manual input, or hidden retry was added.

A means main `ee5ce48b48784f999af374c1452003f8b2b1230f`. **Both measured
rows below are A**, with the exact same executable SHA-256. These are A/A
repeat observations, not baseline/candidate improvement metrics.

| Parent repeat | Process CPU mean (core %) | Frame-log intervals: count / mean / p95 / p99 (ms) | Guest flip cadence (frames/s) | Mesa private files |
| --- | ---: | ---: | ---: | ---: |
| A, repeat 1 | 239.21 | 149 / 2014.46 / 2960.93 / 3160.89 | 0.50826 | 829 |
| A, repeat 2 | 249.45 | 252 / 1193.27 / 1630.25 / 1741.75 | 0.76614 | 833 |
| Corrected candidate B | Not measured | Not measured | Not measured | Not measured |

Repeat 2 versus repeat 1: CPU **+4.28%**, mean frame interval **−40.76%**,
and guest cadence **+50.74%**. These changes occurred without a product change.
They do not establish candidate gains. The second start image shows `GO`;
the first does not. Both show the same parked car at 0 MPH, but the wall-time
navigation/wait procedure did not establish identical guest phase.

**Candidate ABBA/BAAB was not submitted.** A stable affected fixed-work
fixture is needed before interpreting small whole-emulator differences.
The prepared matrix is retained; no pending targets were cancelled or hidden.

## Original outcomes and measurement contract

| Attempt | Immutable run ID | Outcome | Use |
| --- | --- | --- | --- |
| Scene/cache preflight | `20261002-082441071-921c63fd05b4447e9716b01dbf15df1a` | Completed; generic correctness pass; complete; eligible | No saved analysis, `analysis_not_recorded`; preserved without repair |
| A repeat 1 | `20261002-083710607-abf4cfd3882540f5bece0598c161d5aa` | Completed; generic correctness pass; complete; eligible | Stored built-in performance report; phase/variation limitation above |
| A repeat 2 | `20261002-084709096-1333ffdba386450e92f3554a55fcbc84` | Completed; generic correctness pass; complete; eligible | Same saved test/analysis revision as repeat 1 |

The original preflight omitted a saved analysis contract. A new immutable
saved test revision adds post-exit analysis; it retains identical runtime
inputs, plan, settings, executable, assets and libraries. A too-long saved-test
description was rejected before native execution, then shortened on the same
draft. The configuration rejection is retained separately.

CPU uses the exact `stationary` segment, with 604 finite samples per repeat.
239% means about 2.39 host CPU cores, not 239% of the whole machine. Guest
frame intervals use the final declared 300 seconds of the frame log; cadence
uses the final six flip records. Their timestamps are their own clock domain.
They are not display presentation and are not automatically aligned to the
host CPU segment. The saved 300-second dwell keeps them in the parked scene,
but its start-phase mismatch remains explicit. No native audio oracle exists.

Mesa's original ledgers report `mesa_private_disk_writes_observed`, verified
and comparison-ready for all three runs, with no explicitly accepted waiver.
OS page cache and driver memory cache remain uncontrolled as recorded by the
runner. Generic runner eligibility does not certify native ADPCM parity or
solve the same-parent variation.

## Build and attribution checks

Parent executable: `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`.
Prepared candidate: `d09e9c77d61c7322ef7510781bbd93484967c8cd986c0ec0569ed3da08c87bd7`;
product revision `95f889090c8`, PR head `2a644ef4672`.
[CI36983173251](https://github.com/Mainkill1/xemu/actions/runs/36983173251)
passed all builds/unit tests. CI merge `c16c2b83e17f` has an identical tree to
that PR head. Both debug files identify Clang/LLD21.1.8, Rust1.96.0 and the
same GCC12.3 static component. Candidate Build ID:
`fa81c0246f5c5f7baa04087163431af51f824552`. These identity checks do not prove
performance.

The existing PR293 five-second profile was exported again with its recorded
FP callchains. Of 142 longword-read leaf records, 141 have no caller frame and
one has only an unknown caller. It cannot resolve ADPCM/PCM or descriptor versus
payload reads. This export preserves the original capture/outcome and prevents
treating its 9.31% voice-worker read attribution as this candidate's potential
gain. `attribution/caller-availability.json` and the raw export are included.

## Evidence storage

`EVIDENCE.tar.gz` contains original raw telemetry, stored analysis, captures,
assessments, state ledgers, input/build identities and frozen client recipes.
`INDEX.json` hashes each payload and lists all explicit omissions. Private
Mesa cache bytes and duplicate diagnostic ZIPs remain in the complete local
and server collections; original cache qualification is included. All three
full collections returned `complete=true`, `scope=allEligibleArtifacts`,
`excluded=0`. No ROM, disc, HDD or emulator executable is published here.

Next gates: actual QEMU mapping/concurrency tests, affected ADPCM/XISO/audio
correctness, a stable fixed-work measurement, then physical ABBA and BAAB on
`.123`. PR275 stays draft; PR187 remains deferred. No merge is authorized.
