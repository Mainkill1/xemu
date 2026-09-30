# PR272: inline dispatch followup

## Recommendation: HOLD

The previous owner-counter revision accidentally made GCC outline `tb_lookup` in the enabled build. This followup forces the existing wrapper inline only when the probe is compiled in. The optimized native and Windows binaries now have no outlined `tb_lookup` symbol. The collector, publication interval and cache behavior are unchanged. Twenty-eight Deck attempts passed correctness; every performance comparison remains cache-unqualified. No retention candidate exists.

## Baseline versus candidate: Deck / Vulkan

| Test / fixed work | Baseline | Candidate | Difference | Improvement % |
| --- | ---: | ---: | ---: | ---: |
| rewrite / 1M operations | prior-off 3.053455 s | off 3.064159 s | +0.010704 s | **-0.35%** |
| rewrite / 1M operations | disabled 3.023972 s | off 3.064159 s | +0.040187 s | **-1.33%** |
| rewrite / 1M operations | off 3.064159 s | counters 3.057943 s | -0.006215 s | **+0.20%** |
| rewrite / 1M operations | off 3.064159 s | timing 3.100515 s | +0.036356 s | **-1.19%** |
| rewrite / 1M operations | off 3.064159 s | all 3.103249 s | +0.039091 s | **-1.28%** |
| stable / 50M operations | prior-off 3.441607 s | off 2.586797 s | -0.854811 s | **+24.84%** |
| stable / 50M operations | disabled 2.637186 s | off 2.586797 s | -0.050390 s | **+1.91%** |
| stable / 50M operations | off 2.586797 s | counters 2.703507 s | +0.116710 s | **-4.51%** |
| stable / 50M operations | off 2.586797 s | timing 2.689372 s | +0.102575 s | **-3.97%** |
| stable / 50M operations | off 2.586797 s | all 2.685877 s | +0.099080 s | **-3.83%** |

Positive Improvement means less guest-reported time: `100 × (baseline − candidate) / baseline`. Each cell is the median of two independent process-attempt medians, with ten guest samples per attempt. This is two repetitions, not twenty independent observations. These are fixed-work guest times, not game FPS, host CPU cost, retention savings, or qualified speedups.

### Mode definitions and sources

| Table name | Source / binary | Collection |
| --- | --- | --- |
| disabled | `0bd18bf26b894852f934a231a29a09b2f5eb480c` / `2e99f64682c15b8a90fbe0953af1e997087c67dd34c67c96e8fdd613349ba874` | Compiled out |
| prior-off | `0bd18bf26b894852f934a231a29a09b2f5eb480c` / `97a3264d2bf35ef45f2fdac323482d4b279d20cd577a730477699c2a673ba196` | OFF; previous outlined wrapper |
| off, counters, timing, all | `3a144c6624fec0cd8b926e9956a38740e35167b8` / `77b84f158fd35ddffe1a41ad99d0260d6f75a29e1cce220b5e3e02809023c6f0` | Same revised enabled executable |

`off` collects nothing. `counters` counts lookups and invalidations with periodic owner publication. `timing` adds sparse nanosecond timestamps. `all` also samples occupancy; these modes do not change the clearing writes. `prior-off` is only a comparison label: its actual runtime setting is `off`. Exact `cpu_translation_blocks.code_stable` and `.code_rewrite` IDs are in every observation.

## Controls and limits

Each leaf starts with two identical revised-OFF A/A runs, followed by a symmetric sweep: disabled, prior-off, off, counters, timing, all, all, timing, counters, off, prior-off, disabled. Same base `458730bf53`, compiler, dependencies and immutable firmware/HDD-seed/EEPROM/reference/ISO/catalog/settings; Vulkan, 128MiB, warmups0, multiplier1. No monitor queries or operator events occurred during retained runs. No attempt was excluded from this followup. Collection started after the last attempt completed.

| OFF A/A | First | Second | Apparent Improvement % |
| --- | ---: | ---: | ---: |
| `cpu_translation_blocks.code_rewrite` | 3.046851 s | 3.041193 s | +0.19% |
| `cpu_translation_blocks.code_stable` | 2.579590 s | 2.572986 s | +0.26% |

Power/frequency/thermal state was not locked. DriverNamespaceVerified=false and ComparisonReady=false; no uncontrolled-cache waiver. Two repetitions cannot establish the under-5% gate or worst-case overhead. Raw process metrics lack guest boundaries and thread-specific utilization. Game FPS/frame percentiles, OpenGL/unaffected controls and retention lifetime/remapping/reset/concurrency tests remain outstanding.

## Before and after

```text
Before: cpu_exec_loop -> outlined tb_lookup -> inline probe hooks -> existing cache lookup
After:  cpu_exec_loop contains tb_lookup and probe hooks; no wrapper call
Both:   owner-local lookup counters -> publish every 65,536 starts and at CPU-exec exit
        shared invalidation counters stay atomic; cache validation and clearing stay intact
```

The source patch changes code placement, including cold/miss paths; it is not a semantic cache optimization. Disabled builds do not apply the annotation. [Source patch](build/source-change.patch), [native build and symbols](build/deck-receipt.json), [Windows build and symbols](build/windows-receipt.json). Existing 9 collector / 6 production-helper tests and 9-test ASan/UBSan and TSan results apply to [byte-identical collector/test files](build/collector-equivalence.json), as archived in the prior packet. Fresh guest checks exercise the changed wrapper. The [pre-commit native compile](build/inline-build-native-dirty.log) logged four existing GL/Vulkan shadow warnings and no modified-file warning. The committed-source rebuild updated version objects and linked the measured binary; Windows compiled the modified wrapper successfully with unrelated warnings. Both logs are retained.

## Windows and previous findings

The Windows revised binary built successfully, but no revised guest results exist. The runner previously rejected clean rewrite attempt3 before execution: 1,061,818,368 bytes free versus 1,073,741,824 required. The HTTP API has no supported cleanup action; no storage gate or SSH bypass was used. Historical Windows stable counter/all slowdowns remain in the [owner-accounting packet](../research245-owner-probe-20260930/REPORT.md); they measure `0bd18bf2`, not this revised binary. That packet also preserves 40 excluded completed attempts, all failures, host ablations and parent controls. Its 54.18% tight-loop accounting improvement and 100.38% residual compiled-out-to-counter overhead are standalone measurements, not emulator improvements.

## Reproduction and complete observations

Run `python3 docs/evidence/research245-inline-probe-20260930/audit.py` from the repository. [Observation pins](observations.json), [comparison arithmetic](comparisons.json), [A/A controls](aa-controls.json), [artifact inventory](selected-artifact-inventory.json), [receipt](receipt.json), [all hashes](SHA256SUMS). Immutable plans and runner inventories are under `deck/plans/`; selected raw text under `deck/runs/`. Binary/cache payloads were not downloaded.

| Order | Leaf | Mode | Median | Raw result |
| --- | --- | --- | ---: | --- |
| 1 | `cpu_translation_blocks.code_stable` | off | 2.579590 s | [20260930-205903124-ce3c14535df24deb9774f99ef77d7857](deck/runs/20260930-205903124-ce3c14535df24deb9774f99ef77d7857/guest/normalized-results.json) |
| 2 | `cpu_translation_blocks.code_stable` | off | 2.572986 s | [20260930-205945782-9296e0ed75444bc993ea16d5c8337179](deck/runs/20260930-205945782-9296e0ed75444bc993ea16d5c8337179/guest/normalized-results.json) |
| 3 | `cpu_translation_blocks.code_stable` | disabled | 2.596265 s | [20260930-210028387-2a2544567c1347a491c0e5913e85683e](deck/runs/20260930-210028387-2a2544567c1347a491c0e5913e85683e/guest/normalized-results.json) |
| 4 | `cpu_translation_blocks.code_stable` | prior-off | 3.442605 s | [20260930-210111612-29022d4582234533a7ac393e7ee530e4](deck/runs/20260930-210111612-29022d4582234533a7ac393e7ee530e4/guest/normalized-results.json) |
| 5 | `cpu_translation_blocks.code_stable` | off | 2.578088 s | [20260930-210202903-d03896d746d2464d9ccfb90b34bf5fdd](deck/runs/20260930-210202903-d03896d746d2464d9ccfb90b34bf5fdd/guest/normalized-results.json) |
| 6 | `cpu_translation_blocks.code_stable` | counters | 2.681712 s | [20260930-210246096-1f22098f9f624641ad824e280d6b8825](deck/runs/20260930-210246096-1f22098f9f624641ad824e280d6b8825/guest/normalized-results.json) |
| 7 | `cpu_translation_blocks.code_stable` | timing | 2.675769 s | [20260930-210330340-7b1a75b564bc44ceafc9e940d5daaf84](deck/runs/20260930-210330340-7b1a75b564bc44ceafc9e940d5daaf84/guest/normalized-results.json) |
| 8 | `cpu_translation_blocks.code_stable` | all | 2.650921 s | [20260930-210414318-a353e61406344274a72141a8f3574d61](deck/runs/20260930-210414318-a353e61406344274a72141a8f3574d61/guest/normalized-results.json) |
| 9 | `cpu_translation_blocks.code_stable` | all | 2.720833 s | [20260930-210458246-623aa92c31bb4056b4f4bb2d527c9341](deck/runs/20260930-210458246-623aa92c31bb4056b4f4bb2d527c9341/guest/normalized-results.json) |
| 10 | `cpu_translation_blocks.code_stable` | timing | 2.702974 s | [20260930-210542480-190676c4937b401f8f10eb639dd8b346](deck/runs/20260930-210542480-190676c4937b401f8f10eb639dd8b346/guest/normalized-results.json) |
| 11 | `cpu_translation_blocks.code_stable` | counters | 2.725302 s | [20260930-210626820-5c8be82e39f446d1abba87b7d8240042](deck/runs/20260930-210626820-5c8be82e39f446d1abba87b7d8240042/guest/normalized-results.json) |
| 12 | `cpu_translation_blocks.code_stable` | off | 2.595505 s | [20260930-210711281-e1b6de2202084c7da2f515c93d4247b9](deck/runs/20260930-210711281-e1b6de2202084c7da2f515c93d4247b9/guest/normalized-results.json) |
| 13 | `cpu_translation_blocks.code_stable` | prior-off | 3.440609 s | [20260930-210754203-52a48836637e4505990996cc5ccb81e6](deck/runs/20260930-210754203-52a48836637e4505990996cc5ccb81e6/guest/normalized-results.json) |
| 14 | `cpu_translation_blocks.code_stable` | disabled | 2.678109 s | [20260930-210845948-c9adfdbe3e574266a734f18a9e720f05](deck/runs/20260930-210845948-c9adfdbe3e574266a734f18a9e720f05/guest/normalized-results.json) |
| 1 | `cpu_translation_blocks.code_rewrite` | off | 3.046851 s | [20260930-210930105-c8bd946ecbcb4ccca7f3c7f2683a92a5](deck/runs/20260930-210930105-c8bd946ecbcb4ccca7f3c7f2683a92a5/guest/normalized-results.json) |
| 2 | `cpu_translation_blocks.code_rewrite` | off | 3.041193 s | [20260930-211017542-081323a78d624aebbe66213d40ae04c9](deck/runs/20260930-211017542-081323a78d624aebbe66213d40ae04c9/guest/normalized-results.json) |
| 3 | `cpu_translation_blocks.code_rewrite` | disabled | 2.976211 s | [20260930-211105221-e7ed858872524498b11341c993078dcd](deck/runs/20260930-211105221-e7ed858872524498b11341c993078dcd/guest/normalized-results.json) |
| 4 | `cpu_translation_blocks.code_rewrite` | prior-off | 3.065433 s | [20260930-211152551-86a703fd53874ba98fa4dc7e195c9d3a](deck/runs/20260930-211152551-86a703fd53874ba98fa4dc7e195c9d3a/guest/normalized-results.json) |
| 5 | `cpu_translation_blocks.code_rewrite` | off | 2.986797 s | [20260930-211240624-2215633b936342158c7baccbe4d2633e](deck/runs/20260930-211240624-2215633b936342158c7baccbe4d2633e/guest/normalized-results.json) |
| 6 | `cpu_translation_blocks.code_rewrite` | counters | 3.047306 s | [20260930-211327996-11bba6474ed040d48c023bd7ddb6b895](deck/runs/20260930-211327996-11bba6474ed040d48c023bd7ddb6b895/guest/normalized-results.json) |
| 7 | `cpu_translation_blocks.code_rewrite` | timing | 3.103925 s | [20260930-211415391-a77a250ee90b44e683c84edb62fad9cc](deck/runs/20260930-211415391-a77a250ee90b44e683c84edb62fad9cc/guest/normalized-results.json) |
| 8 | `cpu_translation_blocks.code_rewrite` | all | 3.114670 s | [20260930-211503233-502bdc6a6fe14f818e376346340c0965](deck/runs/20260930-211503233-502bdc6a6fe14f818e376346340c0965/guest/normalized-results.json) |
| 9 | `cpu_translation_blocks.code_rewrite` | all | 3.091829 s | [20260930-211551452-45e5da5132ee4aab9a4bd274461010cb](deck/runs/20260930-211551452-45e5da5132ee4aab9a4bd274461010cb/guest/normalized-results.json) |
| 10 | `cpu_translation_blocks.code_rewrite` | timing | 3.097105 s | [20260930-211639923-bce569f855b64ddd8991e63c7e79262b](deck/runs/20260930-211639923-bce569f855b64ddd8991e63c7e79262b/guest/normalized-results.json) |
| 11 | `cpu_translation_blocks.code_rewrite` | counters | 3.068581 s | [20260930-211728284-fc15c27809cb40e2ad6e29f69739fe78](deck/runs/20260930-211728284-fc15c27809cb40e2ad6e29f69739fe78/guest/normalized-results.json) |
| 12 | `cpu_translation_blocks.code_rewrite` | off | 3.141521 s | [20260930-211816488-d2c2619d64744c57b77d271ddce9ca89](deck/runs/20260930-211816488-d2c2619d64744c57b77d271ddce9ca89/guest/normalized-results.json) |
| 13 | `cpu_translation_blocks.code_rewrite` | prior-off | 3.041476 s | [20260930-211904656-c30d3070a2da40febd7bebcbbaff3aff](deck/runs/20260930-211904656-c30d3070a2da40febd7bebcbbaff3aff/guest/normalized-results.json) |
| 14 | `cpu_translation_blocks.code_rewrite` | disabled | 3.071732 s | [20260930-211952490-80f7a474c81c4698b5590c7f9e53a043](deck/runs/20260930-211952490-80f7a474c81c4698b5590c7f9e53a043/guest/normalized-results.json) |

Prepared by Codex (GPT-6). PR remains draft; no merge or ready action.
