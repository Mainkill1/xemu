# Full Speed

`Full-Speed` is the consolidated performance branch for the xemu changes
reviewed after the original Full-Speed baseline (`6234f3606f`). Source
branches remain open: this branch contains the selected production changes,
not replacements for their history.

## Included branches

| Branch | Status in Full-Speed | Change |
| --- | --- | --- |
| `feature/eng-2026-520-perf-etw-event-telemetry` | Included | Adds opt-in NV2A retail-capture telemetry so CPU, OpenGL, and Vulkan work can be aligned with ETW captures without changing normal runtime behavior. |
| `feature/eng-2026-525-vk-wait-telemetry-u524a4d5b` | Superseded by v2 | Introduced per-reason Vulkan submit/wait attribution. Its functional changes are retained through the v2 branch. |
| `feature/eng-2026-525-vk-wait-telemetry-v2` | Included | Reduces telemetry overhead by timing the first occurrences and periodic hot occurrences of each Vulkan wait reason. |
| `feature/eng-2026-523-vk-vertex-upload-staging` | Included | Stages ordered vertex-RAM updates in persistent Vulkan buffers, avoiding unnecessary finish/wait work while retaining bounded capacity behavior. |
| `feature/eng-2026-523-vk-cpu-hotpath-attribution` | Included | Attributes Vulkan draw-preparation CPU work in the opt-in telemetry output. |
| `feature/eng-2026-523-win-qpc-clock-fastpath` | Included | Avoids an unnecessary 128-bit divide in exact Windows QPC scaling. |
| `feature/eng-2026-523-win-nvidia-prefer-max-performance-u90829d4e` | Included | Requests NVIDIA's maximum-performance preferred power state for the xemu profile on Windows. |
| `feature/eng-2026-523-tcg-active-mmu-dirty-reset-ud9e1e15a` | Included | Skips empty MMU modes while resetting code-dirty state. |
| `feature/eng-2026-523-tcg-x86-static-state-tb-lookup-u3121717e` | Included | Final stacked TCG/Vulkan rollup: reuses known x86 translation state, specializes dirty-memory paths, avoids redundant register clearing, stages texture uploads in the ordered draw stream, targets surface callbacks directly, and clears pipeline-change state after binding. |
| `feature/eng-2026-523-vk-native-bc-on-vertex-staging-u8a09a4ea` | Included, conflict-resolved | Uploads supported BC1/BC2/BC3 textures in native Vulkan block-compressed formats rather than decoding them on the CPU; unsupported layouts retain the decoded fallback. It was merged onto the later ordered texture-upload path and includes BC layout unit coverage. |
| `xemu-pr-staging: feature/eng-2026-336-stage-local-vulkan-uniforms-v3-uec89c153` | Included, conflict-resolved | Separates vertex- and pixel-shader uniform source generations so unchanged stages do not rewrite or re-upload their UBOs. It preserves exact float bit patterns, tracks effective polygon-offset inputs, and retains Full-Speed's existing VMState-compatible dirty-row handling. |
| `fix/eng-2026-523-vk-report-dma-ownership` | Included | Captures the report DMA object when a GL, Vulkan, or null-renderer query is queued, preventing a later context switch from writing the completed report through the wrong DMA mapping. |
| `feature/eng-2026-523-vk-texture-pipeline-fastpath-uaad84ed1` | Included | Keeps texture image/sampler changes on the descriptor path instead of forcing a Vulkan pipeline lookup; shader-affecting texture state still invalidates through `ShaderState`. The recorded Morrowind capture reduced pipeline lookups by 79.9%, and the fixed-work A/B comparison improved average throughput by 1.079%. |
| `fix/eng-2026-523-nv2a-ptimer-overdue-catchup-u76925787` | Included | Coalesces missed runtime PTIMER alarm epochs into the next future alarm instead of repeatedly scheduling already-expired alarms. Includes a deterministic overdue-alarm unit test. |

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
| `feature/eng-2026-523-vk-batched-aux-main-submit-u48cab4ee` | Batches the auxiliary and draw command buffers into one ordered submit. Its measured A/B result was neutral/slower, so it remains research. |
| `research/eng-2026-523-vk-report-boundary-submit-u688e8e71` | Records report-boundary submission experiments and remains open pending a reproducible end-to-end improvement. |
| `research/eng-2026-523-vk-tiny-draw-reuse-attribution-u42bc13ac` | Contains further tiny-draw and cache-attribution experiments; it is deliberately not merged wholesale. |

## Display controls

The rollup has one display-side CPU-saving preference:

`Reduce host CPU usage when VSync is off` (`display.window.reduce_host_cpu_usage`)
is disabled by default. When enabled while VSync is off, it both uses efficient
host timer waits and yields briefly between uncapped presentation frames. The
same setting is declared once in `config_spec.yml` and rendered once in the
Display menu; no competing toggle was introduced. The NVIDIA power preference
is automatic on supported Windows NVIDIA systems, and Vulkan telemetry remains
opt-in through `XEMU_VK_PERF_LOG`, so neither adds a second UI control.
The combined Vulkan telemetry record uses schema version 5, which includes
both CPU-region and native-BC upload counters.

The PTIMER validation branch identified a workload-specific regression when
that opt-in CPU-reduction preference is enabled. It remains disabled by
default; retain it as an opt-in control and revalidate it separately before
claiming a general uncapped-performance benefit.

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

## Isolated unsafe-host-load removal branch

This branch differs from `Full-Speed` only by removing the known-bugged
`display.window.reduce_host_cpu_usage` implementation and its process-wide
timer-precision side effect. Compare `dd77ab850a` against `Full-Speed` at
`111deac13f`; the old option must not be enabled as a performance baseline.

Validate release and debug builds with the perf-lab XISO and fresh-boot retail
tests. Confirm the removed preference is neither accepted nor shown, default
VSync-off behavior matches the known-safe path, and guest timing does not
change because of a host-load preference.

The observed host-load reduction remains a valuable feature lead. A
replacement must be developed on a separate single-variable branch as an
explicit presentation-deadline policy, with host CPU usage, frame-time
median/p95/p99, missed presents, audio stability, and guest-timer behavior
measured together. This removal is a safety baseline, not abandonment of that
feature.

## Host-load deadline redesign lane

This branch is the dedicated planning base for restoring an opt-in
`Reduce host load` emulator setting safely. It currently contains no runtime
behavior beyond the parent removal branch. Future implementation commits must
remain isolated here until their A/B/C evidence is complete:

```text
A: Full-Speed 111deac13f with the legacy option disabled
B: safe-removal parent 4140a916fb
C: this branch with the redesigned option enabled
```

The new setting may pace only the presentation/display loop. It must wait on
an actual presentation deadline, event, or measured adaptive budget; it must
not change QEMU timer precision, guest clocks, interrupt scheduling, or audio
pacing, and it must not add an unconditional fixed sleep.

Test both release/LTO and debug builds. Run the perf-lab XISO, fresh-boot PGR2
and Morrowind, and uncapped idle/menu/gameplay scenarios with the setting off
and on. Record host CPU package/process use, render-loop wakeups, requested and
actual wait duration, present cadence, median/p95/p99/max frame time, missed
presents, input latency, audio underruns, and guest timer ticks. The option is
acceptable only if it preserves guest-work counts and timing while producing
a repeatable host-load reduction without worse frame-time tails.

Any later implementation must replace this planning commit rather than
silently reusing the removed process-global timer API.
