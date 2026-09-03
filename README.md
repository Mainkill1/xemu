# Full Speed

`Full-Speed` is the consolidated, reviewable performance branch. It starts
from the original Full-Speed baseline (`6234f3606f`) and includes the current
validated performance work plus the recent correctness/safety repairs required
to test it safely. Source branches remain open so every change can still be
built and tested alone.

This branch is the combined candidate. It is not evidence that every component
improves a workload on its own. The branch matrix below is the required A/B
order: build the upstream baseline and each exact branch head separately, then
test the combined branch after each component has a clean result.

## Included branches

| Branch | Status in Full-Speed | Change |
| --- | --- | --- |
| `feature/eng-2026-520-perf-etw-event-telemetry` | Included | Adds opt-in NV2A retail-capture telemetry so CPU, OpenGL, and Vulkan work can be aligned with ETW captures without changing normal runtime behavior. |
| `feature/eng-2026-525-vk-wait-telemetry-u524a4d5b` | Superseded by v2 | Introduced per-reason Vulkan submit/wait attribution. Its functional changes are retained through the v2 branch. |
| `feature/eng-2026-525-vk-wait-telemetry-v2` | Included | Reduces telemetry overhead by timing the first occurrences and periodic hot occurrences of each Vulkan wait reason. |
| `feature/eng-2026-523-vk-vertex-upload-staging` | Included | Stages ordered vertex-RAM updates in persistent Vulkan buffers, avoiding unnecessary finish/wait work while retaining bounded capacity behavior. |
| `feature/eng-2026-523-vk-cpu-hotpath-attribution` | Included | Attributes Vulkan draw-preparation CPU work in the opt-in telemetry output. |
| `feature/eng-2026-523-win-qpc-clock-fastpath` | Included | Avoids an unnecessary 128-bit divide in exact Windows QPC scaling. |
| `feature/eng-2026-523-win-nvidia-prefer-max-performance-u90829d4e` | Superseded by removal | The old automatic NVIDIA power-policy request is deliberately removed below. It changed persistent host policy and is not part of the final rollup. |
| `feature/eng-2026-523-tcg-active-mmu-dirty-reset-ud9e1e15a` | Included | Skips empty MMU modes while resetting code-dirty state. |
| `feature/eng-2026-523-tcg-x86-static-state-tb-lookup-u3121717e` | Included | Final stacked TCG/Vulkan rollup: reuses known x86 translation state, specializes dirty-memory paths, avoids redundant register clearing, stages texture uploads in the ordered draw stream, targets surface callbacks directly, and clears pipeline-change state after binding. |
| `feature/eng-2026-523-vk-native-bc-on-vertex-staging-u8a09a4ea` | Included, conflict-resolved | Uploads supported BC1/BC2/BC3 textures in native Vulkan block-compressed formats rather than decoding them on the CPU; unsupported layouts retain the decoded fallback. It was merged onto the later ordered texture-upload path and includes BC layout unit coverage. |
| `xemu-pr-staging: feature/eng-2026-336-stage-local-vulkan-uniforms-v3-uec89c153` | Included, conflict-resolved | Separates vertex- and pixel-shader uniform source generations so unchanged stages do not rewrite or re-upload their UBOs. It preserves exact float bit patterns, tracks effective polygon-offset inputs, and retains Full-Speed's existing VMState-compatible dirty-row handling. |
| `fix/eng-2026-523-vk-report-dma-ownership` | Included | Captures the report DMA object when a GL, Vulkan, or null-renderer query is queued, preventing a later context switch from writing the completed report through the wrong DMA mapping. |
| `feature/eng-2026-523-vk-texture-pipeline-fastpath-uaad84ed1` | Included | Keeps texture image/sampler changes on the descriptor path instead of forcing a Vulkan pipeline lookup; shader-affecting texture state still invalidates through `ShaderState`. The recorded Morrowind capture reduced pipeline lookups by 79.9%, and the fixed-work A/B comparison improved average throughput by 1.079%. |
| `fix/eng-2026-523-nv2a-ptimer-overdue-catchup-u76925787` | Included | Coalesces missed runtime PTIMER alarm epochs into the next future alarm instead of repeatedly scheduling already-expired alarms. Includes a deterministic overdue-alarm unit test. |
| `fix/eng-2026-523-vk-batched-submit-safety-u70dcad5c` | Included | Batches compatible auxiliary and draw command buffers only when their ordering dependencies are preserved. The safety repair keeps the required dependency edges. |
| `fix/eng-2026-523-ptimer-reconciliation-zero-ratio-u3f16e05d` | Included | Treats zero PTIMER ratios as a stopped clock and rebuilds the host deadline from restored guest state. The overdue-alarm behavior is represented by the same shared latch/next-alarm logic. |
| `fix/eng-2026-523-callback-retirement-order-u16a212b2` | Included | Defers cached memory-callback release until the exclusive-removal flush barrier completes, preventing a callback from being freed while it can still be observed. |
| `fix/eng-2026-523-remove-nvidia-power-policy-u83f2a080` | Included | Removes the persistent NVIDIA policy mutation. Performance capture must not change a user's driver profile. |
| `fix/eng-2026-523-remove-unsafe-host-load-ube3040c6` | Included | Removes the unsafe host-load/timer control rather than masking its timing side effects. |

## Current independent build matrix

Build every row below from its exact head, alongside the upstream baseline. Do
not merge another review branch into an individual build. A branch can contain
its documented historical prerequisites; that is part of the branch identity,
not an extra integration by the tester.

| Build ID | Exact branch | Why it is tested alone | Full-Speed state |
| --- | --- | --- | --- |
| `baseline-upstream-main` | upstream `d73326b62199c6dd952ef512947710e1333a49d3` | Reference for correctness, performance, and packaging. | Reference only |
| `fix-branch-review-blockers-8718ad8b` | `fix/eng-2026-523-branch-review-blockers-u486f523e` | Aggregate blocker head used to cross-check the isolated repairs. | Documentation/evidence aggregate; unique runtime work is represented below |
| `fix-callback-retirement-order-85383466` | `fix/eng-2026-523-callback-retirement-order-u16a212b2` | Tests callback lifetime and exclusive-flush ordering. | Included |
| `fix-nv2a-ptimer-overdue-catchup-d74e82d1` | `fix/eng-2026-523-nv2a-ptimer-overdue-catchup-u76925787` | Tests missed-epoch alarm coalescing. | Included through shared PTIMER latch logic |
| `fix-ptimer-reconciliation-zero-ratio-b332c16d` | `fix/eng-2026-523-ptimer-reconciliation-zero-ratio-u3f16e05d` | Tests zero-ratio clock handling and restored-deadline reconciliation. | Included |
| `fix-remove-nvidia-power-policy-ede650cf` | `fix/eng-2026-523-remove-nvidia-power-policy-u83f2a080` | Confirms a test build does not persistently mutate NVIDIA policy. | Included |
| `fix-remove-unsafe-host-load-4140a916` | `fix/eng-2026-523-remove-unsafe-host-load-ube3040c6` | Confirms host-load control cannot contaminate capture timing. | Included |
| `fix-vk-batched-submit-safety-2991e35e` | `fix/eng-2026-523-vk-batched-submit-safety-u70dcad5c` | Tests batched-submit ordering and command-buffer dependencies. | Included |
| `fix-vk-report-dma-ownership-16a55523` | `fix/eng-2026-523-vk-report-dma-ownership` | Tests report publication after DMA/context changes. | Included |

Each release and debug package must retain its exact source SHA, branch name,
container digest, build command, build log, executable hash, and test result.
Do not use a combined result to mark an individual branch passed.

## Reviewed research branches

These branches remain open and are recorded here for traceability. They were
not merged wholesale because they are measurements, experiments, or contain
explicitly reverted experiments rather than an additional validated production
change.

| Branch | Result |
| --- | --- |
| `research/eng-2026-523-always-stage-vertex-layout-safe-ue4c43cdc` | Evaluated staging vertex RAM without layout changes; the validated production staging work is represented by the vertex-upload branch above. |
| `research/eng-2026-523-sparse-uniform-layout-safe-u6299d05f` | Investigated safely skipping clean uniform rows; retained as research pending an independently validated landing. |
| `research/eng-2026-523-tcg-tb-lookup-attribution-u065f47f7` | Collected indirect TB-lookup evidence that informed the later TCG fast paths. |
| `research/eng-2026-523-vk-stalled-gpu-timestamps-uf1f85872` | Added diagnostic Vulkan timing experiments and records several reverted candidates; it is intentionally not used as a production rollup branch. |
| `feature/eng-2026-523-vk-aux-submit-fence-u70f4e0ef` | Replaces a queue-wide idle wait with an auxiliary-command-buffer fence. It passed focused validation, but its recorded end-to-end A/B result was neutral to slightly slower; it remains a candidate, not a rollup change. |
| `feature/eng-2026-523-vk-batched-aux-main-submit-u48cab4ee` | Its ordered batching implementation is now included through the explicit safety-fix branch above. The isolated branch still needs its own test result. |
| `research/eng-2026-523-vk-report-boundary-submit-u688e8e71` | Records report-boundary submission experiments and remains open pending a reproducible end-to-end improvement. |
| `research/eng-2026-523-vk-tiny-draw-reuse-attribution-u42bc13ac` | Contains further tiny-draw and cache-attribution experiments; it is deliberately not merged wholesale. |

## Capture controls and safety

Vulkan telemetry remains opt-in through `XEMU_VK_PERF_LOG`. It records work
but must not alter presentation policy, persistent NVIDIA driver policy, or
guest timer semantics. The NVIDIA policy request and the unsafe host-load
timing control are both removed by this branch.

PTIMER now has explicit zero-ratio, post-load, and overdue-alarm coverage.
These paths are correctness gates: a timing improvement is rejected if it
changes the guest-visible alarm, interrupt, report, readback, or reset result.

## Staging audit

The `xemu-pr-staging` production candidates were compared against this
rollup. The transient-buffer growth, changed-uniform-row, versioned-vertex,
dynamic-blend, and texture-dirty branches already have later validated
successors here. ENG-2026-336 was the remaining distinct production change;
its seven focused unit tests passed on the Linux dev box under Wine before it
was reconciled into this branch. Staging source branches remain open.

## Validation focus

Validate this rollup with the same snapshot and emulator configuration used by
the baseline. Compare guest-event median, p95, p99, and maximum duration;
Vulkan finish/wait time; ordered texture-upload and vertex-staging counters;
native-versus-decoded BC upload counters; and TCG CPU time. Keep the source
branches available for A/B comparison rather than closing or deleting them.
For the new work, also run `test-xbox-nv2a-ptimer`, verify report-query writes
against the DMA object active at queue time, and compare Vulkan pipeline
lookups plus draw-preparation CPU time on a texture-heavy saved-state capture.
