# Windows: matched-source observer and parent controls

## Recommendation: HOLD

34 completed guest attempts: 14 clean retained, 20 excluded. Correctness PASS for every executed leaf; comparisons are cache-unqualified. Guest source is `0bd18bf2` on parent `458730bf53`; earlier-source attempts are explicitly excluded.

| Test ID / Vulkan | Baseline | Candidate | Guest median difference | Improvement % | Correctness / qualification |
| --- | ---: | ---: | ---: | ---: | --- |
| `cpu_translation_blocks.code_stable` | disabled 1.080810 s | off 1.105094 s | +0.024284 s | **-2.25%** | PASS / ineligible |
| `cpu_translation_blocks.code_stable` | off 1.105094 s | counters 1.382116 s | +0.277022 s | **-25.07%** | PASS / ineligible |
| `cpu_translation_blocks.code_stable` | off 1.105094 s | timing 1.405775 s | +0.300681 s | **-27.21%** | PASS / ineligible |
| `cpu_translation_blocks.code_stable` | off 1.105094 s | all 1.452083 s | +0.346989 s | **-31.40%** | PASS / ineligible |

Positive Improvement means less guest-reported work time. Median of two independent process-attempt medians; each attempt contains ten samples of 50M stable-code or 1M rewrite operations. This is two repetitions per setting, not twenty independent observations. These are guest fixed-work times, not host CPU cost, FPS, game frame time, or cache-retention savings. Matched compiler/options/dependencies and immutable firmware/seed/EEPROM/reference/ISO/catalog/settings; 128MiB RAM, Vulkan, warmups0, multiplier1; no HMP queries. Source and executable pins appear in every observation and raw launch/input receipts. Hosts are not compared to one another.

`parent` is exact upstream458730bf; `disabled` is candidate source with probe compilation disabled; `off` is the enabled executable with collection OFF. Active modes use the same enabled executable. The mode sweep is symmetric disabled/OFF/counters/timing/all/all/timing/counters/OFF/disabled, preceded by two OFF A/A attempts per leaf. Parent controls use parent/disabled/disabled/parent. No small difference is accepted as a production improvement. Power/frequency/thermal state was not locked. Cache qualification failed, and no waiver is enabled.

## Identical OFF A/A

| Leaf | First | Second | Apparent Improvement % |
| --- | ---: | ---: | ---: |
| `cpu_translation_blocks.code_stable` | 1.087567 s | 1.127001 s | -3.63% |
| `cpu_translation_blocks.code_rewrite` | 2.504223 s | 2.085453 s | +16.72% |

## Missing Windows comparisons

The runner rejected the third clean rewrite attempt before execution because available storage was 1,061,818,368 bytes, below its 1,073,741,824-byte floor. Remaining rewrite modes and all clean parent controls were not launched. The HTTP API has no supported storage cleanup action. No gate was lowered and no SSH bypass was used. See [structured preflight rejection](plans/clean-rewrite/preflight-operation-failure.json) and [nonexecuted plans](nonexecuted-attempts.json).

## Exclusions and all executed attempts

An upstream advance required new binaries. Four Deck/six Windows earlier-source attempts were retained separately. A wait on a prepared campaign returned `attention`, not terminal; the local followup launcher mistakenly inserted two parent-control attempts per host during the first rewrite sweep. Every original rewrite attempt and those preludes are excluded from clean arithmetic, regardless of outcome. Deck completed a new clean rewrite sweep. Windows completed only its two clean A/A attempts before the storage rejection. Two excluded Windows rewrite runs also record bulk transfers: 2 events in `20260930-200820303-006706e3f1134096ab232990474308fb`, and 21 in `20260930-201235351-493c5cc582bc461eb027e0b8b3585e66`. All 14 retained Windows runs have no operator intervention. No unfavorable observation is removed from the archive.

| Group / order | Leaf | Mode | Guest median | Inclusion | Run / raw normalized result |
| --- | --- | --- | ---: | --- | --- |
| 20260930 / 1 | `cpu_translation_blocks.code_stable` | off | 1.087567 s | retained | [20260930-200121367-ab81afb49ef4402eb81849df320b324f](runs/20260930-200121367-ab81afb49ef4402eb81849df320b324f/guest/normalized-results.json) |
| 20260930 / 2 | `cpu_translation_blocks.code_stable` | off | 1.127001 s | retained | [20260930-200148251-ce0d4ba08e9742a3b772c019f3b87c95](runs/20260930-200148251-ce0d4ba08e9742a3b772c019f3b87c95/guest/normalized-results.json) |
| 20260930 / 3 | `cpu_translation_blocks.code_stable` | disabled | 1.078243 s | retained | [20260930-200215746-f768f4a5283d4aaf87f1573f9651eab5](runs/20260930-200215746-f768f4a5283d4aaf87f1573f9651eab5/guest/normalized-results.json) |
| 20260930 / 4 | `cpu_translation_blocks.code_stable` | off | 1.109225 s | retained | [20260930-200242713-82d3483d8add4ef5a23b40453c71103c](runs/20260930-200242713-82d3483d8add4ef5a23b40453c71103c/guest/normalized-results.json) |
| 20260930 / 5 | `cpu_translation_blocks.code_stable` | counters | 1.380975 s | retained | [20260930-200309532-b34356a49f22448fbbac12660a692345](runs/20260930-200309532-b34356a49f22448fbbac12660a692345/guest/normalized-results.json) |
| 20260930 / 6 | `cpu_translation_blocks.code_stable` | timing | 1.415631 s | retained | [20260930-200339717-ec1fea2a56fb484e86bbc0fb6ae88de3](runs/20260930-200339717-ec1fea2a56fb484e86bbc0fb6ae88de3/guest/normalized-results.json) |
| 20260930 / 7 | `cpu_translation_blocks.code_stable` | all | 1.476856 s | retained | [20260930-200409733-18b84954cb5c4c26b2bff38299caf56e](runs/20260930-200409733-18b84954cb5c4c26b2bff38299caf56e/guest/normalized-results.json) |
| 20260930 / 8 | `cpu_translation_blocks.code_stable` | all | 1.427310 s | retained | [20260930-200440349-f8aa1865ed1747189119fd32cdd7a337](runs/20260930-200440349-f8aa1865ed1747189119fd32cdd7a337/guest/normalized-results.json) |
| 20260930 / 9 | `cpu_translation_blocks.code_stable` | timing | 1.395918 s | retained | [20260930-200510340-4514394781f3449a96b56d96a6ba4012](runs/20260930-200510340-4514394781f3449a96b56d96a6ba4012/guest/normalized-results.json) |
| 20260930 / 10 | `cpu_translation_blocks.code_stable` | counters | 1.383257 s | retained | [20260930-200540171-0dccded5721b4f64b487670f19733bc9](runs/20260930-200540171-0dccded5721b4f64b487670f19733bc9/guest/normalized-results.json) |
| 20260930 / 11 | `cpu_translation_blocks.code_stable` | off | 1.100963 s | retained | [20260930-200610328-cf6a21cc16d1428e9885b02ab3235c7d](runs/20260930-200610328-cf6a21cc16d1428e9885b02ab3235c7d/guest/normalized-results.json) |
| 20260930 / 12 | `cpu_translation_blocks.code_stable` | disabled | 1.083377 s | retained | [20260930-200637195-9175701d425c4988bfe3ed865ffbc7d8](runs/20260930-200637195-9175701d425c4988bfe3ed865ffbc7d8/guest/normalized-results.json) |
| 20260930 / 1 | `cpu_translation_blocks.code_rewrite` | off | 1.907053 s | excluded | [20260930-200703900-e32336a5a3ac48359a88b1d6e909d584](runs/20260930-200703900-e32336a5a3ac48359a88b1d6e909d584/guest/normalized-results.json) |
| 20260930 / 2 | `cpu_translation_blocks.code_rewrite` | off | 2.478055 s | excluded | [20260930-200739223-01158a2ae1224f6c985d400ac1cdfc87](runs/20260930-200739223-01158a2ae1224f6c985d400ac1cdfc87/guest/normalized-results.json) |
| 20260930 / 3 | `cpu_translation_blocks.code_rewrite` | disabled | 2.671700 s | excluded | [20260930-200820303-006706e3f1134096ab232990474308fb](runs/20260930-200820303-006706e3f1134096ab232990474308fb/guest/normalized-results.json) |
| 20260930 / 4 | `cpu_translation_blocks.code_rewrite` | off | 2.502623 s | excluded | [20260930-200903155-d8303964eda14e5a8c462a5d221ef1e3](runs/20260930-200903155-d8303964eda14e5a8c462a5d221ef1e3/guest/normalized-results.json) |
| 20260930 / 5 | `cpu_translation_blocks.code_rewrite` | counters | 2.507308 s | excluded | [20260930-200944051-40fa6801fc5d450fb708d84dcb338888](runs/20260930-200944051-40fa6801fc5d450fb708d84dcb338888/guest/normalized-results.json) |
| 20260930 / 6 | `cpu_translation_blocks.code_rewrite` | timing | 2.554788 s | excluded | [20260930-201051499-711b9dc747664939bcea95f450aaa843](runs/20260930-201051499-711b9dc747664939bcea95f450aaa843/guest/normalized-results.json) |
| 20260930 / 7 | `cpu_translation_blocks.code_rewrite` | all | 1.972573 s | excluded | [20260930-201159295-6fac6522d0be44fc9ffe5cbb75bbbef0](runs/20260930-201159295-6fac6522d0be44fc9ffe5cbb75bbbef0/guest/normalized-results.json) |
| 20260930 / 8 | `cpu_translation_blocks.code_rewrite` | all | 2.521594 s | excluded | [20260930-201235351-493c5cc582bc461eb027e0b8b3585e66](runs/20260930-201235351-493c5cc582bc461eb027e0b8b3585e66/guest/normalized-results.json) |
| 20260930 / 9 | `cpu_translation_blocks.code_rewrite` | timing | 1.907606 s | excluded | [20260930-201316451-a52714b7bd3c43a38a884d2fb3db309f](runs/20260930-201316451-a52714b7bd3c43a38a884d2fb3db309f/guest/normalized-results.json) |
| 20260930 / 10 | `cpu_translation_blocks.code_rewrite` | counters | 1.894862 s | excluded | [20260930-201351953-5d243b0c21bc4e67a895a9961bd26bfa](runs/20260930-201351953-5d243b0c21bc4e67a895a9961bd26bfa/guest/normalized-results.json) |
| 20260930 / 11 | `cpu_translation_blocks.code_rewrite` | off | 2.496152 s | excluded | [20260930-201426767-f4954b8bfd4e4d7a9fda893f6cd3c354](runs/20260930-201426767-f4954b8bfd4e4d7a9fda893f6cd3c354/guest/normalized-results.json) |
| 20260930 / 12 | `cpu_translation_blocks.code_rewrite` | disabled | 2.525929 s | excluded | [20260930-201507611-9009fb85133e4c5a8047bd797a05e1bc](runs/20260930-201507611-9009fb85133e4c5a8047bd797a05e1bc/guest/normalized-results.json) |
| clean-rewrite / 1 | `cpu_translation_blocks.code_rewrite` | off | 2.504223 s | retained | [20260930-201552239-ec4afa6df635480a83a8afc7b989ab34](runs/20260930-201552239-ec4afa6df635480a83a8afc7b989ab34/guest/normalized-results.json) |
| clean-rewrite / 2 | `cpu_translation_blocks.code_rewrite` | off | 2.085453 s | retained | [20260930-201633213-020b02fafde54efc8e9817e582f7ad60](runs/20260930-201633213-020b02fafde54efc8e9817e582f7ad60/guest/normalized-results.json) |
| superseded-aa8bb9ab / 1 | `cpu_translation_blocks.code_stable` | off | 1.112567 s | excluded | [20260930-195234585-8e8273553d7742b290a183f3729fa76a](runs/20260930-195234585-8e8273553d7742b290a183f3729fa76a/guest/normalized-results.json) |
| superseded-aa8bb9ab / 2 | `cpu_translation_blocks.code_stable` | off | 1.121090 s | excluded | [20260930-195301555-92a4bcc581014de69e89adcb2c3fe1fb](runs/20260930-195301555-92a4bcc581014de69e89adcb2c3fe1fb/guest/normalized-results.json) |
| superseded-aa8bb9ab / 3 | `cpu_translation_blocks.code_stable` | disabled | 1.223558 s | excluded | [20260930-195328504-6a40ea077e754ead94c0fc7bdb35798c](runs/20260930-195328504-6a40ea077e754ead94c0fc7bdb35798c/guest/normalized-results.json) |
| superseded-aa8bb9ab / 4 | `cpu_translation_blocks.code_stable` | off | 1.118974 s | excluded | [20260930-195356941-e858864187974fb28ca2b907999232a1](runs/20260930-195356941-e858864187974fb28ca2b907999232a1/guest/normalized-results.json) |
| superseded-aa8bb9ab / 5 | `cpu_translation_blocks.code_stable` | counters | 1.357509 s | excluded | [20260930-195423851-39c135d093db4647b0baf54382f4e3f3](runs/20260930-195423851-39c135d093db4647b0baf54382f4e3f3/guest/normalized-results.json) |
| superseded-aa8bb9ab / 6 | `cpu_translation_blocks.code_stable` | timing | 1.418705 s | excluded | [20260930-195453773-b54c1b8d83ce47ed9d1e434d96b5a803](runs/20260930-195453773-b54c1b8d83ce47ed9d1e434d96b5a803/guest/normalized-results.json) |
| parent-prelude / 1 | `cpu_translation_blocks.code_stable` | parent | 1.076999 s | excluded | [20260930-201024861-356b74bce50a484ba171ddac083ebfb9](runs/20260930-201024861-356b74bce50a484ba171ddac083ebfb9/guest/normalized-results.json) |
| parent-prelude / 2 | `cpu_translation_blocks.code_stable` | disabled | 1.059569 s | excluded | [20260930-201132912-5745a6cda0334f29afd60e55fea1f9be](runs/20260930-201132912-5745a6cda0334f29afd60e55fea1f9be/guest/normalized-results.json) |

[All observations and exclusion reasons](observations.json), [full comparison arithmetic](comparisons.json), [A/A controls](aa-controls.json), [receipt](receipt.json), [selected artifact hashes](selected-artifact-inventory.json), [unexecuted plans](nonexecuted-attempts.json). Complete eligible inventory metadata and original immutable plans are under `plans/`; only canonical text files were downloaded. Raw metrics.csv retains sampled process CPU utilization, sampler duty and memory. It does not identify guest workload boundaries or thread-specific utilization; host CPU per guest operation and game FPS/p95/p99 remain unqualified. OpenGL/unaffected graphics/game controls and production lifetime/remapping/reset/concurrency safety remain outstanding.
