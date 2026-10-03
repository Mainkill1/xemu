# PGR2 voice formats on Steam Deck

Draft [PR275](https://github.com/Mainkill1/xemu/pull/275) accelerates encoded-word reads for **non-streaming ADPCM**. This diagnostic establishes that the parked PGR2 workload configures that format. It does not establish a whole-game speedup, audio parity, or the fraction of physical-read cycles attributable to those voices.

## Observed formats and their relationship to the patch

At one paused snapshot, 79 voices were active, unpaused and not multipass:

| Configured format | Voices | PR275 reader applies? | Relationship to existing measurement |
| --- | ---: | --- | --- |
| Non-streaming mono ADPCM, one block channel | **45** | Yes, when encoded blocks are fetched | Same **9 words / 36 bytes** as the measured mono component |
| Non-streaming PCM, B16 container | 16 | No | PCM reader unchanged |
| Non-streaming PCM, B32 container | 6 | No | PCM reader unchanged |
| Streaming mono PCM, B16 container | 11 | No | Streaming reader unchanged |
| Streaming stereo ADPCM, two block channels | 1 | No | Existing direct-copy streaming branch unchanged |
| **Total** | **79** | | |

The separately measured mono component improved **48.04%** in balanced ABBA/BAAB. That figure is time saved per encoded-block read, **not** a predicted PGR2 improvement. This snapshot contains no affected stereo ADPCM voice; the 54.35% stereo component result remains synthetic coverage. Counts of configured voices do not measure their block-read frequency or CPU cost.

[Component A/B table, exact executable and raw balanced runs](../issue197-deck-component-20261002/README.md). [Expanded original/candidate correctness, CI and Deck controls](../issue197-expanded-correctness-20261002/README.md).

## Method and identities

- Host: **Steam Deck `10.0.0.123`**, Linux HTTP runner `0.2.0+f5b3e58de71ce6fb56450679a456fd049aa95b29`; no Windows run.
- Original main reference: `ee5ce48b48784f999af374c1452003f8b2b1230f`; executable SHA-256 `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`. Runtime files were reused from the existing pinned original PGR2 package; no candidate ran here.
- Job: `issue197-deck-pgr2-voice-snapshot-v1-20261002`; run: `20261002-112947954-1d5e49263604423ba3977956a37a93a0`.
- The existing fixed boot/controller route and Vulkan settings were retained, including 128 MiB guest memory and full DSP/JIT. The parked segment ran from **11:32:01.503 to 11:37:02.948 UTC**, just over 300 seconds, before the diagnostic postlude.
- Direct inspection of the end screenshot confirms the expected parked red car at **0 MPH**, with opponents departed. Sound output was not listened to or compared.

After pausing, `info pci` confirms MCPX APU device `10de:01b0` and BAR0 `0xfe800000`. Reading `NV_PAPU_VPVADDR` at `0xfe80202c` returns `0x038c8000`. The helper validates that aligned address within guest RAM, then uses the existing QMP `pmemsave` diagnostic to read exactly **32,768 bytes**, representing 256 voice records of 128 bytes each. All three diagnostics complete. Snapshot SHA-256: `142e973de6887dc337ba08b9862a83faa0fdebebf85038e882a98b185ab54ea9`.

The parser uses the production register layout: format word at offset `0x04`, state word at `0x54`, active bit 21, paused bit 18, container bits 30–31, streaming bit 24, stereo bit 27, and multipass bit 21 in the format word. An independent byte-slice reparse of the original dump confirms the helper's 79/45 counts. In `voice_get_samples()`, non-streaming ADPCM fetches `9 * samples_per_block` words through the changed reader. The observed mono voices have `samples_per_block == 1`.

## Original outcomes and limits

| Outcome | Recorded state |
| --- | --- |
| Execution | Completed, exit **0**, declared plan completed |
| Screenshot correctness | Passed |
| Diagnostic PCI/base/table reads | All completed |
| Mesa control | Private cold namespace verified; `mesa_private_disk_writes_observed`; no uncontrolled-cache waiver |
| Collection | **878 files**, 7,811,555 bytes; complete, **zero excluded** |
| Performance evidence | **Incomplete / comparison ineligible**: insufficient positive frame intervals in the inherited final-300-second analysis window |

The diagnostic pause interrupts that end-of-run frame window. Its failure is preserved; no timing or speedup is claimed from this run. A separate helper error initially left the VM paused: scripted `wait` steps freeze while paused. Observation timed out while the same native job remained live. The operator resumed that same job through the authorized HTTP control; its postlude and quit then completed. No native attempt was restarted, and no existing assessment was repaired. The earlier 240-character saved-description rejection also remains in the archive; it occurred before launch.

The installed immutable diagnostic test is `issue197-pgr2-voice-snapshot-v1`, revision `1afe1f6fd5638a3952782e8d2b8918fb701d682217496823071b4b7022dc0235`, with all 39 executable/library build slots. The retained helper now accepts a fresh `--id` and `--output`, or `--observe` for a submitted saved test. Its future v2 diagnostic definition removes performance analysis and resumes after capture. **That future v2 definition has not been executed or installed**; v1's original failure state stays unchanged. The archive includes the initial helper and corrected helper; the latter received CLI/syntax checks only after this run.

This is one original-build register snapshot. It does not prove actual fetch counts, candidate voice behavior, mixed output, listening quality, arbitrary concurrency safety, or steady-state performance. The cause of earlier baseline variation remains unresolved. PR275 stays draft **HOLD** for matched game/audio and affected/control XISO qualification.

## Retained evidence

[`EVIDENCE.tar.gz`](EVIDENCE.tar.gz) contains original diagnostic bytes/receipts, all voice records, screenshots, raw telemetry/frame logs, settings, state ledger, canonical failed assessment, helper failures and immutable-test receipt. [`INDEX.json`](INDEX.json) hashes every archived file and lists omitted runtime guest/cache state with hashes; those files remain retained locally. All 878 eligible artifacts were collected before packaging. No BIOS, game disc or HDD asset payload is published. [`SUMMARY.json`](SUMMARY.json) provides machine-readable counts and exact identities. The archive was reopened and every payload hash verified.
