# Deck: matched-source observer and parent controls

## Recommendation: HOLD

54 completed guest attempts: 34 clean retained, 20 excluded. Correctness PASS for every executed leaf; comparisons are cache-unqualified. Guest source is `0bd18bf2` on parent `458730bf53`; earlier-source attempts are explicitly excluded.

| Test ID / Vulkan | Baseline | Candidate | Guest median difference | Improvement % | Correctness / qualification |
| --- | ---: | ---: | ---: | ---: | --- |
| `cpu_translation_blocks.code_stable` | disabled 2.587425 s | off 3.422808 s | +0.835383 s | **-32.29%** | PASS / ineligible |
| `cpu_translation_blocks.code_stable` | off 3.437851 s | counters 3.423826 s | -0.014026 s | **+0.41%** | PASS / ineligible |
| `cpu_translation_blocks.code_stable` | off 3.437851 s | timing 3.427633 s | -0.010218 s | **+0.30%** | PASS / ineligible |
| `cpu_translation_blocks.code_stable` | off 3.437851 s | all 3.444631 s | +0.006779 s | **-0.20%** | PASS / ineligible |
| `cpu_translation_blocks.code_stable` | parent 2.591588 s | disabled 2.575938 s | -0.015649 s | **+0.60%** | PASS / ineligible |
| `cpu_translation_blocks.code_rewrite` | disabled 3.032210 s | off 2.971599 s | -0.060611 s | **+2.00%** | PASS / ineligible |
| `cpu_translation_blocks.code_rewrite` | off 2.971599 s | counters 3.047435 s | +0.075837 s | **-2.55%** | PASS / ineligible |
| `cpu_translation_blocks.code_rewrite` | off 2.971599 s | timing 3.090297 s | +0.118698 s | **-3.99%** | PASS / ineligible |
| `cpu_translation_blocks.code_rewrite` | off 2.971599 s | all 3.258400 s | +0.286801 s | **-9.65%** | PASS / ineligible |
| `cpu_translation_blocks.code_rewrite` | parent 3.039094 s | disabled 2.978347 s | -0.060748 s | **+2.00%** | PASS / ineligible |

Positive Improvement means less guest-reported work time. Median of two independent process-attempt medians; each attempt contains ten samples of 50M stable-code or 1M rewrite operations. This is two repetitions per setting, not twenty independent observations. These are guest fixed-work times, not host CPU cost, FPS, game frame time, or cache-retention savings. Matched compiler/options/dependencies and immutable firmware/seed/EEPROM/reference/ISO/catalog/settings; 128MiB RAM, Vulkan, warmups0, multiplier1; no HMP queries. Source and executable pins appear in every observation and raw launch/input receipts. Hosts are not compared to one another.

`parent` is exact upstream458730bf; `disabled` is candidate source with probe compilation disabled; `off` is the enabled executable with collection OFF. Active modes use the same enabled executable. The mode sweep is symmetric disabled/OFF/counters/timing/all/all/timing/counters/OFF/disabled, preceded by two OFF A/A attempts per leaf. Parent controls use parent/disabled/disabled/parent. No small difference is accepted as a production improvement. Power/frequency/thermal state was not locked. Cache qualification failed, and no waiver is enabled.

## Identical OFF A/A

| Leaf | First | Second | Apparent Improvement % |
| --- | ---: | ---: | ---: |
| `cpu_translation_blocks.code_stable` | 3.446490 s | 3.408810 s | +1.09% |
| `cpu_translation_blocks.code_rewrite` | 3.056695 s | 3.008218 s | +1.59% |

## Exclusions and all executed attempts

An upstream advance required new binaries. Four Deck/six Windows earlier-source attempts were retained separately. A wait on a prepared campaign returned `attention`, not terminal; the local followup launcher mistakenly inserted two parent-control attempts per host during the first rewrite sweep. Every original rewrite attempt and those preludes are excluded from clean arithmetic, regardless of outcome. Recorded bulk transfers also occurred during excluded rewrite runs; all clean retained runs are checked for intervention. Deck completed a new clean rewrite sweep. Windows completed only its two clean A/A attempts before the storage rejection. The Deck dormant pair was also replaced with a clean four-run ABBA control because its final compiled-out stable run recorded31 bulk-transfer events; both original compiled-out observations are excluded. No unfavorable observation is removed from the archive.

| Group / order | Leaf | Mode | Guest median | Inclusion | Run / raw normalized result |
| --- | --- | --- | ---: | --- | --- |
| 20260930 / 1 | `cpu_translation_blocks.code_stable` | off | 3.446490 s | retained | [20260930-195825883-bd509d921e114440abe85927467f0eac](runs/20260930-195825883-bd509d921e114440abe85927467f0eac/guest/normalized-results.json) |
| 20260930 / 2 | `cpu_translation_blocks.code_stable` | off | 3.408810 s | retained | [20260930-195918026-6ec72d137b47496d84b5dafbc3439448](runs/20260930-195918026-6ec72d137b47496d84b5dafbc3439448/guest/normalized-results.json) |
| 20260930 / 3 | `cpu_translation_blocks.code_stable` | disabled | 2.580690 s | excluded | [20260930-200009582-8bbad1b4d7e046ba9a0948997dc79f5e](runs/20260930-200009582-8bbad1b4d7e046ba9a0948997dc79f5e/guest/normalized-results.json) |
| 20260930 / 4 | `cpu_translation_blocks.code_stable` | off | 3.434672 s | retained | [20260930-200052855-dd9bd7257f9f4ae5a694da7eea2fb11e](runs/20260930-200052855-dd9bd7257f9f4ae5a694da7eea2fb11e/guest/normalized-results.json) |
| 20260930 / 5 | `cpu_translation_blocks.code_stable` | counters | 3.417838 s | retained | [20260930-200144028-3b60af7dbfeb4add8333dded14c35843](runs/20260930-200144028-3b60af7dbfeb4add8333dded14c35843/guest/normalized-results.json) |
| 20260930 / 6 | `cpu_translation_blocks.code_stable` | timing | 3.419891 s | retained | [20260930-200235524-1c69c327b96341f587e186ce5f09f546](runs/20260930-200235524-1c69c327b96341f587e186ce5f09f546/guest/normalized-results.json) |
| 20260930 / 7 | `cpu_translation_blocks.code_stable` | all | 3.421913 s | retained | [20260930-200327251-2f6f806682b642bd91baee89e79bf2ed](runs/20260930-200327251-2f6f806682b642bd91baee89e79bf2ed/guest/normalized-results.json) |
| 20260930 / 8 | `cpu_translation_blocks.code_stable` | all | 3.467348 s | retained | [20260930-200418806-d6e7f05a9124434392b6b84bc8481fc9](runs/20260930-200418806-d6e7f05a9124434392b6b84bc8481fc9/guest/normalized-results.json) |
| 20260930 / 9 | `cpu_translation_blocks.code_stable` | timing | 3.435376 s | retained | [20260930-200510824-37aad9da0504469e88e2d091dab6ff87](runs/20260930-200510824-37aad9da0504469e88e2d091dab6ff87/guest/normalized-results.json) |
| 20260930 / 10 | `cpu_translation_blocks.code_stable` | counters | 3.429813 s | retained | [20260930-200602565-42281d1431ec48e0a3dd734e76c71484](runs/20260930-200602565-42281d1431ec48e0a3dd734e76c71484/guest/normalized-results.json) |
| 20260930 / 11 | `cpu_translation_blocks.code_stable` | off | 3.441031 s | retained | [20260930-200653718-047bff3aa8b94a3b9be463e95599dcb9](runs/20260930-200653718-047bff3aa8b94a3b9be463e95599dcb9/guest/normalized-results.json) |
| 20260930 / 12 | `cpu_translation_blocks.code_stable` | disabled | 2.581704 s | excluded | [20260930-200745542-4c241f47af5d44409a6e510ebeeb95c4](runs/20260930-200745542-4c241f47af5d44409a6e510ebeeb95c4/guest/normalized-results.json) |
| 20260930 / 1 | `cpu_translation_blocks.code_rewrite` | off | 3.028945 s | excluded | [20260930-200828829-36e858b392b348f78d613f84fd6055e4](runs/20260930-200828829-36e858b392b348f78d613f84fd6055e4/guest/normalized-results.json) |
| 20260930 / 2 | `cpu_translation_blocks.code_rewrite` | off | 3.098002 s | excluded | [20260930-200916575-4aee3c7c42034a65a84dd47e07f1c70d](runs/20260930-200916575-4aee3c7c42034a65a84dd47e07f1c70d/guest/normalized-results.json) |
| 20260930 / 3 | `cpu_translation_blocks.code_rewrite` | disabled | 3.021673 s | excluded | [20260930-201046520-f99c1f73c9bc4fd490080176abac8f84](runs/20260930-201046520-f99c1f73c9bc4fd490080176abac8f84/guest/normalized-results.json) |
| 20260930 / 4 | `cpu_translation_blocks.code_rewrite` | off | 3.007335 s | excluded | [20260930-201217054-88993298bd4c462095b6e9164f69751b](runs/20260930-201217054-88993298bd4c462095b6e9164f69751b/guest/normalized-results.json) |
| 20260930 / 5 | `cpu_translation_blocks.code_rewrite` | counters | 3.032793 s | excluded | [20260930-201304793-a5096cef736a4a0186e84f87cbed560f](runs/20260930-201304793-a5096cef736a4a0186e84f87cbed560f/guest/normalized-results.json) |
| 20260930 / 6 | `cpu_translation_blocks.code_rewrite` | timing | 3.056560 s | excluded | [20260930-201352089-1007de14aebb40f1a2b677be76d825a8](runs/20260930-201352089-1007de14aebb40f1a2b677be76d825a8/guest/normalized-results.json) |
| 20260930 / 7 | `cpu_translation_blocks.code_rewrite` | all | 3.084173 s | excluded | [20260930-201440203-ad1791c2040c4433837f1aa3fe345080](runs/20260930-201440203-ad1791c2040c4433837f1aa3fe345080/guest/normalized-results.json) |
| 20260930 / 8 | `cpu_translation_blocks.code_rewrite` | all | 3.114932 s | excluded | [20260930-201528569-e6bf565e1009467f96dd34228d9ffca8](runs/20260930-201528569-e6bf565e1009467f96dd34228d9ffca8/guest/normalized-results.json) |
| 20260930 / 9 | `cpu_translation_blocks.code_rewrite` | timing | 3.061259 s | excluded | [20260930-201617151-dc20416481a44d3080d3f28d6e541080](runs/20260930-201617151-dc20416481a44d3080d3f28d6e541080/guest/normalized-results.json) |
| 20260930 / 10 | `cpu_translation_blocks.code_rewrite` | counters | 3.026626 s | excluded | [20260930-201705171-62e73a325caf414aa749fe0a876fc558](runs/20260930-201705171-62e73a325caf414aa749fe0a876fc558/guest/normalized-results.json) |
| 20260930 / 11 | `cpu_translation_blocks.code_rewrite` | off | 2.988458 s | excluded | [20260930-201752759-04a7afd4c73f4444863fdfea2ea6973c](runs/20260930-201752759-04a7afd4c73f4444863fdfea2ea6973c/guest/normalized-results.json) |
| 20260930 / 12 | `cpu_translation_blocks.code_rewrite` | disabled | 3.035043 s | excluded | [20260930-201840049-44584ae33ccd47afbfc4c97cf62f410c](runs/20260930-201840049-44584ae33ccd47afbfc4c97cf62f410c/guest/normalized-results.json) |
| clean-rewrite / 1 | `cpu_translation_blocks.code_rewrite` | off | 3.056695 s | retained | [20260930-201932583-8f7d045c03df416188e1a954b7b6bfd7](runs/20260930-201932583-8f7d045c03df416188e1a954b7b6bfd7/guest/normalized-results.json) |
| clean-rewrite / 2 | `cpu_translation_blocks.code_rewrite` | off | 3.008218 s | retained | [20260930-202020577-eaef708bc0814245886b6e066db051b0](runs/20260930-202020577-eaef708bc0814245886b6e066db051b0/guest/normalized-results.json) |
| clean-rewrite / 3 | `cpu_translation_blocks.code_rewrite` | disabled | 3.025759 s | retained | [20260930-202108246-3e2f3820983f4820b5b99dca50246205](runs/20260930-202108246-3e2f3820983f4820b5b99dca50246205/guest/normalized-results.json) |
| clean-rewrite / 4 | `cpu_translation_blocks.code_rewrite` | off | 2.853015 s | retained | [20260930-202156111-b6caba82ab3d4358a4309d872c4982c1](runs/20260930-202156111-b6caba82ab3d4358a4309d872c4982c1/guest/normalized-results.json) |
| clean-rewrite / 5 | `cpu_translation_blocks.code_rewrite` | counters | 3.060974 s | retained | [20260930-202241932-7afc4c757c6a4989a30ad124fb1d4973](runs/20260930-202241932-7afc4c757c6a4989a30ad124fb1d4973/guest/normalized-results.json) |
| clean-rewrite / 6 | `cpu_translation_blocks.code_rewrite` | timing | 3.045454 s | retained | [20260930-202329776-afe323db861e4760bbfd85784c029b34](runs/20260930-202329776-afe323db861e4760bbfd85784c029b34/guest/normalized-results.json) |
| clean-rewrite / 7 | `cpu_translation_blocks.code_rewrite` | all | 3.371583 s | retained | [20260930-202417299-8e82878f1e234d9e9feca335789fc097](runs/20260930-202417299-8e82878f1e234d9e9feca335789fc097/guest/normalized-results.json) |
| clean-rewrite / 8 | `cpu_translation_blocks.code_rewrite` | all | 3.145218 s | retained | [20260930-202507957-317e853c2e63449a887074326e967473](runs/20260930-202507957-317e853c2e63449a887074326e967473/guest/normalized-results.json) |
| clean-rewrite / 9 | `cpu_translation_blocks.code_rewrite` | timing | 3.135139 s | retained | [20260930-202556969-617b1e0ac2e94e7088ec09b4c9a3b40d](runs/20260930-202556969-617b1e0ac2e94e7088ec09b4c9a3b40d/guest/normalized-results.json) |
| clean-rewrite / 10 | `cpu_translation_blocks.code_rewrite` | counters | 3.033896 s | retained | [20260930-202645708-ece46d91e1ba437e9a16f6b6504d7c4d](runs/20260930-202645708-ece46d91e1ba437e9a16f6b6504d7c4d/guest/normalized-results.json) |
| clean-rewrite / 11 | `cpu_translation_blocks.code_rewrite` | off | 3.090183 s | retained | [20260930-202733410-7de2d872e7fd4687b90d8353d4a2ffee](runs/20260930-202733410-7de2d872e7fd4687b90d8353d4a2ffee/guest/normalized-results.json) |
| clean-rewrite / 12 | `cpu_translation_blocks.code_rewrite` | disabled | 3.038661 s | retained | [20260930-202821112-eaa468d252de4e1984d64a2d90983bad](runs/20260930-202821112-eaa468d252de4e1984d64a2d90983bad/guest/normalized-results.json) |
| clean-dormant / 1 | `cpu_translation_blocks.code_stable` | disabled | 2.584185 s | retained | [20260930-203934372-6a5d5b3f9b0f4c7c92c847d8788cbae2](runs/20260930-203934372-6a5d5b3f9b0f4c7c92c847d8788cbae2/guest/normalized-results.json) |
| clean-dormant / 2 | `cpu_translation_blocks.code_stable` | off | 3.425718 s | retained | [20260930-204017116-f5fcdd2d59c848b08c67ea954e773b2a](runs/20260930-204017116-f5fcdd2d59c848b08c67ea954e773b2a/guest/normalized-results.json) |
| clean-dormant / 3 | `cpu_translation_blocks.code_stable` | off | 3.419897 s | retained | [20260930-204108831-15bce1b824864c67ac5eeb0ba1d597de](runs/20260930-204108831-15bce1b824864c67ac5eeb0ba1d597de/guest/normalized-results.json) |
| clean-dormant / 4 | `cpu_translation_blocks.code_stable` | disabled | 2.590664 s | retained | [20260930-204200477-4b9098f2df9c41ef9f2dd562de16a0f6](runs/20260930-204200477-4b9098f2df9c41ef9f2dd562de16a0f6/guest/normalized-results.json) |
| parent-controls / 1 | `cpu_translation_blocks.code_stable` | parent | 2.585206 s | retained | [20260930-202908323-e72cb7ef59cd40e6ba19fe22b2ed32b1](runs/20260930-202908323-e72cb7ef59cd40e6ba19fe22b2ed32b1/guest/normalized-results.json) |
| parent-controls / 2 | `cpu_translation_blocks.code_stable` | disabled | 2.580689 s | retained | [20260930-202951449-6c79f245d5194a9d8ae573f2069e8826](runs/20260930-202951449-6c79f245d5194a9d8ae573f2069e8826/guest/normalized-results.json) |
| parent-controls / 3 | `cpu_translation_blocks.code_stable` | disabled | 2.571188 s | retained | [20260930-203034662-829393a396334f918c2c4b78f59f919f](runs/20260930-203034662-829393a396334f918c2c4b78f59f919f/guest/normalized-results.json) |
| parent-controls / 4 | `cpu_translation_blocks.code_stable` | parent | 2.597968 s | retained | [20260930-203117215-f810d00bacc74956a749b41c81681278](runs/20260930-203117215-f810d00bacc74956a749b41c81681278/guest/normalized-results.json) |
| parent-controls / 1 | `cpu_translation_blocks.code_rewrite` | parent | 3.004440 s | retained | [20260930-203200359-f9f1506bfdca46709539c7bbce2957c5](runs/20260930-203200359-f9f1506bfdca46709539c7bbce2957c5/guest/normalized-results.json) |
| parent-controls / 2 | `cpu_translation_blocks.code_rewrite` | disabled | 2.964872 s | retained | [20260930-203247885-bb6a769fc83d496bada739c859127381](runs/20260930-203247885-bb6a769fc83d496bada739c859127381/guest/normalized-results.json) |
| parent-controls / 3 | `cpu_translation_blocks.code_rewrite` | disabled | 2.991821 s | retained | [20260930-203334983-1838e3e1091a49ef8aacad2e90ecf24e](runs/20260930-203334983-1838e3e1091a49ef8aacad2e90ecf24e/guest/normalized-results.json) |
| parent-controls / 4 | `cpu_translation_blocks.code_rewrite` | parent | 3.073749 s | retained | [20260930-203422585-9958cc1ac15345e4a41de0515071e657](runs/20260930-203422585-9958cc1ac15345e4a41de0515071e657/guest/normalized-results.json) |
| superseded-aa8bb9ab / 1 | `cpu_translation_blocks.code_stable` | off | 3.410169 s | excluded | [20260930-195233618-6ca4388f06c74c9cba79ae79decef123](runs/20260930-195233618-6ca4388f06c74c9cba79ae79decef123/guest/normalized-results.json) |
| superseded-aa8bb9ab / 2 | `cpu_translation_blocks.code_stable` | off | 3.459964 s | excluded | [20260930-195325236-5bb0e5ee59f44f7f92ba020c216e1438](runs/20260930-195325236-5bb0e5ee59f44f7f92ba020c216e1438/guest/normalized-results.json) |
| superseded-aa8bb9ab / 3 | `cpu_translation_blocks.code_stable` | disabled | 2.582498 s | excluded | [20260930-195416688-e1bfe1df4302462791e64552f83ae5fd](runs/20260930-195416688-e1bfe1df4302462791e64552f83ae5fd/guest/normalized-results.json) |
| superseded-aa8bb9ab / 4 | `cpu_translation_blocks.code_stable` | off | 3.427055 s | excluded | [20260930-195459829-8f64641832f84df88c4ef667877c9cac](runs/20260930-195459829-8f64641832f84df88c4ef667877c9cac/guest/normalized-results.json) |
| parent-prelude / 1 | `cpu_translation_blocks.code_stable` | parent | 2.558791 s | excluded | [20260930-201004197-c93f9ec0193f49a2b78acd7a89177b2d](runs/20260930-201004197-c93f9ec0193f49a2b78acd7a89177b2d/guest/normalized-results.json) |
| parent-prelude / 2 | `cpu_translation_blocks.code_stable` | disabled | 2.613398 s | excluded | [20260930-201133584-9fffcf11eba54bfa96a8745fa90601d0](runs/20260930-201133584-9fffcf11eba54bfa96a8745fa90601d0/guest/normalized-results.json) |

[All observations and exclusion reasons](observations.json), [full comparison arithmetic](comparisons.json), [A/A controls](aa-controls.json), [receipt](receipt.json), [selected artifact hashes](selected-artifact-inventory.json), [unexecuted plans](nonexecuted-attempts.json). Complete eligible inventory metadata and original immutable plans are under `plans/`; only canonical text files were downloaded. Raw metrics.csv retains sampled process CPU utilization, sampler duty and memory. It does not identify guest workload boundaries or thread-specific utilization; host CPU per guest operation and game FPS/p95/p99 remain unqualified. OpenGL/unaffected graphics/game controls and production lifetime/remapping/reset/concurrency safety remain outstanding.
