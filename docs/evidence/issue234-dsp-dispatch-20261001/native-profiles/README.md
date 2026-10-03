# Exact-parent Deck backend diagnostics

**Both attempts are completed / source correctness passed / evidence incomplete / comparison ineligible. No candidate executable was run.** They use exact parent executable SHA-256 `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`, embedded source revision `ee5ce48b48784f999af374c1452003f8b2b1230f`, and ELF build ID `7cc47cc0deb81956e198d71935d1a198116cdb02`.

The maintained runner HTTP client created immutable definitions, reused declared assets, uploaded only the C/JIT configuration variation, submitted the prepared jobs, and collected after both targets exited. No SSH launch, intervention, cache waiver, or retry was used. The first preparation incorrectly waited for an operation receipt after synchronous draft creation; it stopped without launching a target. Inspecting that draft and continuing did not create an extra run. The setup-failure log remains in the parent report directory.

Both variants use PGR2, Vulkan, 128 MiB guest RAM, DSP enabled, VP workers 0, vsync off and fullscreen with a 4:3 viewport. The same private EEPROM and disk seed are recorded in each runtime manifest. Each launch declares a private cold Mesa namespace. Scene navigation is followed by a 60 s stationary warmup, segment start, screenshot, synchronous 30 s CPU diagnostic, 90 s wait, screenshot and segment end. Actual segment lengths are about **122.47 s (C)** and **122.68 s (JIT)**, including diagnostic setup/capture. The frame analysis specifically uses the last 90 s. Power/frequency is uncontrolled. The two backend runs are not a balanced backend-performance comparison.

| Backend | CPU samples | Whole C execution function | C selection region | Positive frame intervals in last 90 s | Required | Canonical evidence |
|---|---:|---:|---:|---:|---:|---|
| C | 6,418 | 11.9246% | 3.2005% | 60 | 160 | Incomplete |
| JIT | 8,060 | 0.0000% | 0.0000% | 53 | 160 | Incomplete |

The frame parser accepted all 3,496 C and 3,774 JIT records. Its 90 s queue contains only 60/53 positive intervals, so the failure is genuine insufficient sampling, not a parser error. Counts drop sharply after the initial boot/menu phase; screenshots show the stationary starting-grid scene. The slowdown begins before CPU capture, so these records do not establish that the profiler caused it. The requirement was not reduced and results were not repaired. The earlier [#278 stationary-scene campaign](https://github.com/Mainkill1/xemu/blob/perf/issue229-native-dsp-multiply/docs/evidence/issue229-dsp-multiply-20261001/REPORT.md) used a 300 s frame tail with the same minimum. Its first exact-parent attempt had 252 positive intervals in that 300 s window. Shortening this sparse workload to 90 s explains why these new diagnostic attempts do not meet that unchanged minimum. No game speedup or FPS estimate is derived from these ineligible attempts.

The exact-symbol CPU mapping records cycle-period weights separately from event counts. The C capture has 6,418 samples and the JIT capture 8,060. Anonymous/unresolved samples remain unattributed; no claim assigns all unknown samples to a backend. Named C interpreter functions account for 39.5721% of the C capture; JIT wrappers alone are not the JIT's total cost. Selection ranges use the exact unchanged parent assembly described in the main report. Sampling skid, short duration, cold-launch/cache state and observer overhead limit these diagnostics.

## Download and integrity

- [Raw canonical attempts, complete expanded perf captures, screenshots, frame/flip/CPU logs, input/state inventories, frozen definitions and mapped leaf records](profiles.tar.gz).
- [Summary and region weights](summary.json).
- [Payload hashes and omitted state/duplicate-bundle inventories](manifest.json.gz).
- [Archive verification](verification.json).

Archive: **13,552,547 bytes**, SHA-256 `85d73351715b00f8c3bff1e71c85b6e9ff07a5f911b648c6bf28773ab4c80af0`. All **109 payload files** were reread from the archive and checked against their source hashes. 1666 raw runtime state/cache files are represented by hash inventories and remain local/server-side. Two original ZIP copies are omitted because their complete expanded diagnostic payloads are already included. No firmware, disk image or emulator/debug binary is redistributed in this packet.

## Procedure and attempt identity

C run `20261001-235059559-3e9986b3fc7b4b5e856691af1bdffbe3`; JIT run `20261001-235605297-a17798163c884a8ca1b4cae5b6cba65d`.

[Preparation](issue234-prepare-profiles.py), [maintained-client collection](issue234-collect-profiles.py), and [exact-binary sample mapping/independent interval counts](issue234-map-profiles.py) are retained. Their paths identify this workspace and the matching parent ELF/debug files. They are procedural records, not permission to restart existing completed IDs. Native candidate performance must use a separately frozen, balanced, sufficiently sampled procedure without diagnostic capture in timed benchmark windows.
