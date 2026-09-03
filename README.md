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
