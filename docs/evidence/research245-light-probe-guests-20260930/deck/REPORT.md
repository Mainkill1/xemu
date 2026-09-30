# Deck: same-executable observer modes

## Recommendation: HOLD

**Summary:** All 24 leaf attempts completed with correctness PASS and complete evidence. These fixed-work timings measure enabled collector cost on the same executable.
**Remaining:** Driver-cache qualification, more repetitions for small mode differences, matched parent versus compiled-OFF builds, graphics/OpenGL/game checks and an actual safety-qualified retention candidate.

| XISO test (Vulkan) | Reference mode / time | Candidate mode / time | Delta | Improvement % | Correctness / qualification |
| --- | ---: | ---: | ---: | ---: | --- |
| `cpu_translation_blocks.code_stable` | OFF 3.411576 s | counters 3.664517 s | +0.252941 s | -7.41% | PASS / unqualified |
| `cpu_translation_blocks.code_stable` | OFF 3.411576 s | occupancy 3.654908 s | +0.243332 s | -7.13% | PASS / unqualified |
| `cpu_translation_blocks.code_stable` | OFF 3.411576 s | timing 3.633239 s | +0.221663 s | -6.50% | PASS / unqualified |
| `cpu_translation_blocks.code_stable` | OFF 3.411576 s | all 3.629059 s | +0.217483 s | -6.37% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 3.035197 s | counters 3.246993 s | +0.211796 s | -6.98% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 3.035197 s | occupancy 3.098021 s | +0.062825 s | -2.07% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 3.035197 s | timing 3.113730 s | +0.078533 s | -2.59% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 3.035197 s | all 3.153729 s | +0.118532 s | -3.91% | PASS / unqualified |

Positive Improvement means less time. Every negative result is retained. Statistic: median of two independent run medians; each run has ten fixed-work guest samples. These are only two process repetitions per mode, not twenty independent runs. Frequency, thermal state and other host activity were not locked. Small differences between collector modes are not ranked as improvements.

## Controls and pins

Warmups=0, multiplier=1, per-iteration completion. Stable-code: 50 million fixed operations/sample; code-rewrite: one million. Same source and executable per host, fixture ISO/catalog, 128 MiB guest RAM, firmware, private HDD seed, EEPROM, pinned reference and Vulkan config. No monitor queries or operator intervention. Two OFF A/A attempts precede a symmetric OFF/counters/occupancy/timing/all/all/timing/occupancy/counters/OFF sweep for each leaf.

| Identical OFF A/A | First | Second | Apparent Improvement % |
| --- | ---: | ---: | ---: |
| `cpu_translation_blocks.code_stable` | 3.420176 s | 3.389976 s | +0.88% |
| `cpu_translation_blocks.code_rewrite` | 3.021847 s | 3.034230 s | -0.41% |

A/A labels contain no change. Driver namespace verification is missing and no uncontrolled-driver-cache waiver is enabled. The runner marks every attempt comparison-ineligible. The table therefore describes observations, not accepted production speedups. Host inventories and every comparison rejection remain in the run evidence. Hosts use different platform builds and are not compared to one another as an optimization.

## Every attempt

| Order | Leaf | Mode | Guest median | Samples | Correctness / evidence | Run / normalized result |
| ---: | --- | --- | ---: | ---: | --- | --- |
| 1 | `code_stable` | OFF A/A 1 | 3.420176 s | 10 | PASS / complete | [20260930-174302271-ae4b5461c97945a6bdadd272a3c2db24](runs/20260930-174302271-ae4b5461c97945a6bdadd272a3c2db24/guest/normalized-results.json) |
| 2 | `code_stable` | OFF A/A 2 | 3.389976 s | 10 | PASS / complete | [20260930-174354057-bfb303599cb54987bdf00136b74b624c](runs/20260930-174354057-bfb303599cb54987bdf00136b74b624c/guest/normalized-results.json) |
| 3 | `code_stable` | off | 3.413142 s | 10 | PASS / complete | [20260930-174445446-60bc2dd929ab4a559d7847a14ed52ada](runs/20260930-174445446-60bc2dd929ab4a559d7847a14ed52ada/guest/normalized-results.json) |
| 4 | `code_stable` | counters | 3.698075 s | 10 | PASS / complete | [20260930-174537060-b89d8f9687a749cdbff5c49cd447c91d](runs/20260930-174537060-b89d8f9687a749cdbff5c49cd447c91d/guest/normalized-results.json) |
| 5 | `code_stable` | occupancy | 3.639894 s | 10 | PASS / complete | [20260930-174631001-ff609f314824496080173d4eee697908](runs/20260930-174631001-ff609f314824496080173d4eee697908/guest/normalized-results.json) |
| 6 | `code_stable` | timing | 3.615677 s | 10 | PASS / complete | [20260930-174725038-747a43aea66545e2bb3f0a5a5884aa94](runs/20260930-174725038-747a43aea66545e2bb3f0a5a5884aa94/guest/normalized-results.json) |
| 7 | `code_stable` | all | 3.622468 s | 10 | PASS / complete | [20260930-174818484-081f1845f5004c15a14455cd8f317e77](runs/20260930-174818484-081f1845f5004c15a14455cd8f317e77/guest/normalized-results.json) |
| 8 | `code_stable` | all | 3.635651 s | 10 | PASS / complete | [20260930-174912737-4937e2cd44e7416a85c28a561c8df1a8](runs/20260930-174912737-4937e2cd44e7416a85c28a561c8df1a8/guest/normalized-results.json) |
| 9 | `code_stable` | timing | 3.650801 s | 10 | PASS / complete | [20260930-175006931-01dae975da6b460b8f7cb6960fd8fa7b](runs/20260930-175006931-01dae975da6b460b8f7cb6960fd8fa7b/guest/normalized-results.json) |
| 10 | `code_stable` | occupancy | 3.669922 s | 10 | PASS / complete | [20260930-175100849-0de47460dfb44a0dbb21846477f8c15f](runs/20260930-175100849-0de47460dfb44a0dbb21846477f8c15f/guest/normalized-results.json) |
| 11 | `code_stable` | counters | 3.630959 s | 10 | PASS / complete | [20260930-175155305-baf3844455de4080900f88b984705099](runs/20260930-175155305-baf3844455de4080900f88b984705099/guest/normalized-results.json) |
| 12 | `code_stable` | off | 3.410010 s | 10 | PASS / complete | [20260930-175249636-6be3cfebacfe4b48a2c1d455d9824931](runs/20260930-175249636-6be3cfebacfe4b48a2c1d455d9824931/guest/normalized-results.json) |
| 1 | `code_rewrite` | OFF A/A 1 | 3.021847 s | 10 | PASS / complete | [20260930-175341517-b36dce44920442f0bceba4732803b43a](runs/20260930-175341517-b36dce44920442f0bceba4732803b43a/guest/normalized-results.json) |
| 2 | `code_rewrite` | OFF A/A 2 | 3.034230 s | 10 | PASS / complete | [20260930-175429813-2dff9849c21c47b99425da2df6ed18a2](runs/20260930-175429813-2dff9849c21c47b99425da2df6ed18a2/guest/normalized-results.json) |
| 3 | `code_rewrite` | off | 3.063976 s | 10 | PASS / complete | [20260930-175518116-e67a1ecea2e44b91a26d91fa208d2ccc](runs/20260930-175518116-e67a1ecea2e44b91a26d91fa208d2ccc/guest/normalized-results.json) |
| 4 | `code_rewrite` | counters | 3.130196 s | 10 | PASS / complete | [20260930-175606497-ca3e814c28d04ff3a863d41ce215f9e3](runs/20260930-175606497-ca3e814c28d04ff3a863d41ce215f9e3/guest/normalized-results.json) |
| 5 | `code_rewrite` | occupancy | 3.121296 s | 10 | PASS / complete | [20260930-175654917-2563be5e64c0463c9d967a58e646c467](runs/20260930-175654917-2563be5e64c0463c9d967a58e646c467/guest/normalized-results.json) |
| 6 | `code_rewrite` | timing | 3.075074 s | 10 | PASS / complete | [20260930-175743858-50d659a5a0bd409ca4a023ef47ae0259](runs/20260930-175743858-50d659a5a0bd409ca4a023ef47ae0259/guest/normalized-results.json) |
| 7 | `code_rewrite` | all | 3.157941 s | 10 | PASS / complete | [20260930-175831828-04f52a7b877c4db38d5ef944511232b1](runs/20260930-175831828-04f52a7b877c4db38d5ef944511232b1/guest/normalized-results.json) |
| 8 | `code_rewrite` | all | 3.149516 s | 10 | PASS / complete | [20260930-175921016-862abcc656b2458497be3f19c5f372a4](runs/20260930-175921016-862abcc656b2458497be3f19c5f372a4/guest/normalized-results.json) |
| 9 | `code_rewrite` | timing | 3.152385 s | 10 | PASS / complete | [20260930-180010189-03c3ee56f2584958ab8844b257720090](runs/20260930-180010189-03c3ee56f2584958ab8844b257720090/guest/normalized-results.json) |
| 10 | `code_rewrite` | occupancy | 3.074746 s | 10 | PASS / complete | [20260930-180059302-d2ca39df1a694c1990efcbc4f7508554](runs/20260930-180059302-d2ca39df1a694c1990efcbc4f7508554/guest/normalized-results.json) |
| 11 | `code_rewrite` | counters | 3.363789 s | 10 | PASS / complete | [20260930-180147153-bcb975fcebda472db4ab70e267e14d38](runs/20260930-180147153-bcb975fcebda472db4ab70e267e14d38/guest/normalized-results.json) |
| 12 | `code_rewrite` | off | 3.006417 s | 10 | PASS / complete | [20260930-180238473-47651deb88c5499c99d282cea2e03393](runs/20260930-180238473-47651deb88c5499c99d282cea2e03393/guest/normalized-results.json) |

[Raw observations](observations.json), [full comparison arithmetic](comparisons.json), [A/A](aa-controls.json), [source/executable receipt](receipt.json), [planned order](schedule.json), [canonical artifact inventory and exclusions](canonical-inventory.json). The parent report links the historical collector and previous-main failures; these current runs do not replace those outcomes or reference hashes. No lazy-retention candidate was tested.
