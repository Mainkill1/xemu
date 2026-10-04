# Equal voice store game qualification on Steam Deck

**Decision: HOLD / draft [PR276](https://github.com/Mainkill1/xemu/pull/276).** The two baseline controls fail the frozen repeatability gate. No candidate run or ABBA/BAAB slot was launched; this workload cannot establish a candidate game gain.
The matched production VP fixture still establishes its separate fixed-work gains. This game campaign does not qualify audio, guest-writer concurrency or affected XISO references/timings.

## Results at a glance

A means original physical stores; B means the existing equal-RAM-store candidate. Both builds use the same compiler, libraries, game inputs and Vulkan settings. Process CPU is the sampled mean in the stationary segment, expressed as core percent (100% = one core). The control-event statistics measure PGRAPH_INCREMENT intervals, not display presentation or game FPS.

| Baseline control | Mean CPU core % | Mean control interval ms | p95 ms | p99 ms | Positive intervals | Mesa cache | Scene |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| 01-A | 244.767 | 1578.067 | 2258.805 | 2433.564 | 191 | mesa_private_disk_writes_observed | admitted |
| 02-A | 246.230 | 1428.946 | 2238.104 | 2517.794 | 210 | mesa_private_disk_writes_observed | admitted |

The two-run A/A spreads use `100*(maximum-minimum)/median`; both mean CPU and mean control interval must be at most 5%. This is a preliminary repeatability check, not a population confidence bound.

| Gate | Observed spread | Frozen limit |
| --- | ---: | ---: |
| Mean PGRAPH control interval | 9.918% | 5% |
| Mean process CPU | 0.596% | 5% |

Controls qualified: **False**. Candidate slots submitted: **0 / 8**. No failed attempt is discarded and the gate is not relaxed.

## Frozen workload and cache conditions

The maintained parked PGR2 course uses keyboard input with the canonical LStickRight selection and ten A presses. It excludes 60 seconds of warmup after the final input and then records 300 seconds with no acceleration/braking. Start and end captures are inspected after exit. Image admission only confirms the parked race scene; the runner’s coarse nonblack image checks alone cannot establish the scene.

Control 1 shows a GO overlay at the start boundary; control 2 does not. Both show the intended parked race and the same track, but fixed host-clock warmup does not prove identical guest progress. The cause of the 9.92% mean-interval variation is unclassified; these observations do not isolate power, rendering, startup phase or any individual subsystem.

Full DSP, default JIT, VP worker setting 0, Vulkan, vsync off, 128 MiB guest RAM, private EEPROM and HDD seed, identical BIOS/bootrom/disc assets. All 39 executable/library slots are declared before launch. The saved test, definition, input manifest, procedure checks, settings hashes, and physical order are retained. No game asset or private EEPROM bytes are published.

Mesa cache qualification is enforced with no waiver: empty private disk namespace before launch, actual recognized writes afterward, full bounded file ledger, schema 2. OS/driver RAM caches and Deck power/affinity remain uncontrolled. Older cache-ineligible or wrong-scene attempts remain historical failures and are not qualified retroactively.

## Source and build identity

- Original production: `ee5ce48b48784f999af374c1452003f8b2b1230f`.
- Matched reference input: `dd1a8f48690803975d9e97b8e1139dcc4164fc83`.
- Matched candidate input: `5d832c02c3ed6e1f56ef1b808fe74d8f3ea565ee`.
- Full xemu A SHA256: `48e5a5986d11558d47e0966d6df7108f803e5df5b6c902a85a84b0fa7ce06340`.
- Full xemu B SHA256: `3e3c0fbbd84e7f3fb858ca6990c0546ab7c5fea543a8bda5a956893ce8cd037b`.
- Clang/LLD 21.1.8, release ThinLTO, x86 version 3; all 38 runtime libraries match.
- Native host: Steam Deck **10.0.0.123**, maintained HTTP runner **f5b3e58**, port 9368. No Windows or SSH native execution.

Production hardware sources at publication remain byte-identical to the measured candidate. The #197 reader patch is in neither build. Prior CI source/build qualification is copied into this packet; these are the matched full xemu binaries from that qualification, not the standalone VP executables.

## Every native attempt

| Phase | Slot | Run ID | Outcome | Collected files | Mesa files before and after |
| --- | --- | --- | --- | ---: | ---: |
| controls | 01-A | 20261002-184920761-5e122219381449e19ed9703a8f5e9636 | completed/passed/complete/eligible | 862 | 0 → 830 |
| controls | 02-A | 20261002-185847126-77f6b7b8836c45cb89262ce473546f12 | completed/passed/complete/eligible | 865 | 0 → 833 |

Jobs are submitted once and observed through their original handles. Full paged collection happens after confirmed exit; no live previews, diagnostic probes, manual inputs or bulk transfers are allowed. Independent Python recalculation verifies raw source hashes, CPU statistics and control-event mean/p95/p99 against the runner report. Both control records are index-eligible and their test, environment and build keys match. The full reference index has two records and no further page. No baseline pin is changed. A/A controls are not pooled into candidate medians.

The existing frame log is emitted by NV_PGRAPH_INCREMENT. Its label and the runner’s cadence_fps field do not establish displayed FPS. The CPU window is the declared host segment; control intervals use the final 300 seconds of the timestamped log, as specified by the immutable analysis profile. Tail-window and segment boundaries are distinct.

## Remaining qualification

Keep PR276 draft. Complete guest-writer/observer ordering, sample ordering, audible affected audio and live reset/save-load coverage, plus approved affected XISO references and usable per-leaf timings. If game repeatability fails, investigate the measured workload before a new separately frozen campaign; do not hide this gate failure with replacement runs.

[Complete packet](EVIDENCE.tar.gz), [payload and omission index](INDEX.json), [all attempts and adjacent-pair calculations](SUMMARY.json).

Archive SHA256: `a44b6be12833a9e817c4c589badf2a998476fb6724bc66e65db0db25ac37ebb5`. Raw cache binaries, large builds and private EEPROM are retained locally with hashes and explicit omission reasons; complete raw metrics, control logs, ledgers, screenshots, requests, receipts and reusable scripts are included.
