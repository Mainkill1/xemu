# Windows: same-executable observer modes

## Recommendation: HOLD

**Summary:** All 24 leaf attempts completed with correctness PASS and complete evidence. These fixed-work timings measure enabled collector cost on the same executable.
**Remaining:** Driver-cache qualification, more repetitions for small mode differences, matched parent versus compiled-OFF builds, graphics/OpenGL/game checks and an actual safety-qualified retention candidate.

| XISO test (Vulkan) | Reference mode / time | Candidate mode / time | Delta | Improvement % | Correctness / qualification |
| --- | ---: | ---: | ---: | ---: | --- |
| `cpu_translation_blocks.code_stable` | OFF 1.188613 s | counters 1.410690 s | +0.222077 s | -18.68% | PASS / unqualified |
| `cpu_translation_blocks.code_stable` | OFF 1.188613 s | occupancy 1.409471 s | +0.220858 s | -18.58% | PASS / unqualified |
| `cpu_translation_blocks.code_stable` | OFF 1.188613 s | timing 1.469498 s | +0.280885 s | -23.63% | PASS / unqualified |
| `cpu_translation_blocks.code_stable` | OFF 1.188613 s | all 1.452770 s | +0.264157 s | -22.22% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 2.213222 s | counters 2.234883 s | +0.021662 s | -0.98% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 2.213222 s | occupancy 2.513278 s | +0.300057 s | -13.56% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 2.213222 s | timing 2.559939 s | +0.346718 s | -15.67% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | OFF 2.213222 s | all 2.256276 s | +0.043054 s | -1.95% | PASS / unqualified |

Positive Improvement means less time. Every negative result is retained. Statistic: median of two independent run medians; each run has ten fixed-work guest samples. These are only two process repetitions per mode, not twenty independent runs. Frequency, thermal state and other host activity were not locked. Small differences between collector modes are not ranked as improvements.

## Controls and pins

Warmups=0, multiplier=1, per-iteration completion. Stable-code: 50 million fixed operations/sample; code-rewrite: one million. Same source and executable per host, fixture ISO/catalog, 128 MiB guest RAM, firmware, private HDD seed, EEPROM, pinned reference and Vulkan config. No monitor queries or operator intervention. Two OFF A/A attempts precede a symmetric OFF/counters/occupancy/timing/all/all/timing/occupancy/counters/OFF sweep for each leaf.

| Identical OFF A/A | First | Second | Apparent Improvement % |
| --- | ---: | ---: | ---: |
| `cpu_translation_blocks.code_stable` | 1.128964 s | 1.144753 s | -1.40% |
| `cpu_translation_blocks.code_rewrite` | 2.516578 s | 2.535609 s | -0.76% |

A/A labels contain no change. Driver namespace verification is missing and no uncontrolled-driver-cache waiver is enabled. The runner marks every attempt comparison-ineligible. The table therefore describes observations, not accepted production speedups. Host inventories and every comparison rejection remain in the run evidence. Hosts use different platform builds and are not compared to one another as an optimization.

## Every attempt

| Order | Leaf | Mode | Guest median | Samples | Correctness / evidence | Run / normalized result |
| ---: | --- | --- | ---: | ---: | --- | --- |
| 1 | `code_stable` | OFF A/A 1 | 1.128964 s | 10 | PASS / complete | [20260930-180710663-30ca29dd8a404c5a90f0bc03f9c5149a](runs/20260930-180710663-30ca29dd8a404c5a90f0bc03f9c5149a/guest/normalized-results.json) |
| 2 | `code_stable` | OFF A/A 2 | 1.144753 s | 10 | PASS / complete | [20260930-180737811-b44bb1a2c5c846599074c696c7778f46](runs/20260930-180737811-b44bb1a2c5c846599074c696c7778f46/guest/normalized-results.json) |
| 3 | `code_stable` | off | 1.235046 s | 10 | PASS / complete | [20260930-180805145-1a9ddf64ffca43b99372af09bdc9429e](runs/20260930-180805145-1a9ddf64ffca43b99372af09bdc9429e/guest/normalized-results.json) |
| 4 | `code_stable` | counters | 1.429077 s | 10 | PASS / complete | [20260930-180833339-44eb75bf75fd4f6d9a0ff55e0f782d8a](runs/20260930-180833339-44eb75bf75fd4f6d9a0ff55e0f782d8a/guest/normalized-results.json) |
| 5 | `code_stable` | occupancy | 1.396311 s | 10 | PASS / complete | [20260930-180903560-c516c3b346b241009184cb3f29d551b5](runs/20260930-180903560-c516c3b346b241009184cb3f29d551b5/guest/normalized-results.json) |
| 6 | `code_stable` | timing | 1.483413 s | 10 | PASS / complete | [20260930-180933380-4718eb93f8824ffcb563a5f1aee9544c](runs/20260930-180933380-4718eb93f8824ffcb563a5f1aee9544c/guest/normalized-results.json) |
| 7 | `code_stable` | all | 1.439711 s | 10 | PASS / complete | [20260930-181004004-9b7e3419fcaa44b29adca4daacca73ed](runs/20260930-181004004-9b7e3419fcaa44b29adca4daacca73ed/guest/normalized-results.json) |
| 8 | `code_stable` | all | 1.465829 s | 10 | PASS / complete | [20260930-181034294-f5ff7cc9ab8b4a0fa87ff239893462ec](runs/20260930-181034294-f5ff7cc9ab8b4a0fa87ff239893462ec/guest/normalized-results.json) |
| 9 | `code_stable` | timing | 1.455583 s | 10 | PASS / complete | [20260930-181104865-856cd41d4db3443299e5654ecf7a06e9](runs/20260930-181104865-856cd41d4db3443299e5654ecf7a06e9/guest/normalized-results.json) |
| 10 | `code_stable` | occupancy | 1.422632 s | 10 | PASS / complete | [20260930-181135288-6e3dbe4468e64391915cfc544f218a23](runs/20260930-181135288-6e3dbe4468e64391915cfc544f218a23/guest/normalized-results.json) |
| 11 | `code_stable` | counters | 1.392302 s | 10 | PASS / complete | [20260930-181205240-e0aca9849dc5498cb99f7afac93780a0](runs/20260930-181205240-e0aca9849dc5498cb99f7afac93780a0/guest/normalized-results.json) |
| 12 | `code_stable` | off | 1.142179 s | 10 | PASS / complete | [20260930-181235023-b44c5feabb274ce29c937b179f28a893](runs/20260930-181235023-b44c5feabb274ce29c937b179f28a893/guest/normalized-results.json) |
| 1 | `code_rewrite` | OFF A/A 1 | 2.516578 s | 10 | PASS / complete | [20260930-181302242-697b46c893834feab6c763361fe15562](runs/20260930-181302242-697b46c893834feab6c763361fe15562/guest/normalized-results.json) |
| 2 | `code_rewrite` | OFF A/A 2 | 2.535609 s | 10 | PASS / complete | [20260930-181343245-fbb035c573ac415f98acd9094e07ba88](runs/20260930-181343245-fbb035c573ac415f98acd9094e07ba88/guest/normalized-results.json) |
| 3 | `code_rewrite` | off | 1.903693 s | 10 | PASS / complete | [20260930-181424911-605dc38409f14d33b50116cd4e2561b8](runs/20260930-181424911-605dc38409f14d33b50116cd4e2561b8/guest/normalized-results.json) |
| 4 | `code_rewrite` | counters | 2.537669 s | 10 | PASS / complete | [20260930-181459865-e78b1984e96645989abad8633fee7ffd](runs/20260930-181459865-e78b1984e96645989abad8633fee7ffd/guest/normalized-results.json) |
| 5 | `code_rewrite` | occupancy | 2.513257 s | 10 | PASS / complete | [20260930-181541208-bc952da36f5b40c2bc21cc3ea99893af](runs/20260930-181541208-bc952da36f5b40c2bc21cc3ea99893af/guest/normalized-results.json) |
| 6 | `code_rewrite` | timing | 2.530674 s | 10 | PASS / complete | [20260930-181622720-ff53c2b1d11d4ba49127f2f27f0dc955](runs/20260930-181622720-ff53c2b1d11d4ba49127f2f27f0dc955/guest/normalized-results.json) |
| 7 | `code_rewrite` | all | 2.566423 s | 10 | PASS / complete | [20260930-181703798-e7202023211e49ada10dd56b41813ea3](runs/20260930-181703798-e7202023211e49ada10dd56b41813ea3/guest/normalized-results.json) |
| 8 | `code_rewrite` | all | 1.946128 s | 10 | PASS / complete | [20260930-181745324-302683b12e204d16bc1676c6aa4c1adc](runs/20260930-181745324-302683b12e204d16bc1676c6aa4c1adc/guest/normalized-results.json) |
| 9 | `code_rewrite` | timing | 2.589205 s | 10 | PASS / complete | [20260930-181820517-9e46fe549f2c4060a751d89f4330e5f4](runs/20260930-181820517-9e46fe549f2c4060a751d89f4330e5f4/guest/normalized-results.json) |
| 10 | `code_rewrite` | occupancy | 2.513299 s | 10 | PASS / complete | [20260930-181902745-7e89d5ddcbbd4fb8801cbdd8f2172007](runs/20260930-181902745-7e89d5ddcbbd4fb8801cbdd8f2172007/guest/normalized-results.json) |
| 11 | `code_rewrite` | counters | 1.932097 s | 10 | PASS / complete | [20260930-181943706-009806e1315c454797da6457d5ed0f0e](runs/20260930-181943706-009806e1315c454797da6457d5ed0f0e/guest/normalized-results.json) |
| 12 | `code_rewrite` | off | 2.522750 s | 10 | PASS / complete | [20260930-182019303-6dbb479f19c44ab4966d79dbb8b7341c](runs/20260930-182019303-6dbb479f19c44ab4966d79dbb8b7341c/guest/normalized-results.json) |

[Raw observations](observations.json), [full comparison arithmetic](comparisons.json), [A/A](aa-controls.json), [source/executable receipt](receipt.json), [planned order](schedule.json), [canonical artifact inventory and exclusions](canonical-inventory.json). The parent report links the historical collector and previous-main failures; these current runs do not replace those outcomes or reference hashes. No lazy-retention candidate was tested.
