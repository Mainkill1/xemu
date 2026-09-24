# Issue 198: qualify and repair ubershader loading stalls

**Status: Draft investigation and opt-in performance policy. Source-level coverage and scheduling limits identified; the reported title hitches are not yet attributed or fixed.**

Tracks [#198][issue]. This draft adds an investigation and implementation handoff only. It changes no renderer behavior, defaults, or tests. Keep the issue open. Subsequent implementation should be split by demonstrated cause rather than merging every experiment described here.

**Owner direction, 2026-09-24:** users may explicitly prioritize responsiveness over complete rendering, including several seconds of black output while compilation continues. An opt-in Advanced shader-miss policy is an accepted feature direction. Earlier blanket prohibitions on skipping draws are withdrawn; unchanged-output requirements apply to the accuracy-preserving path, not to the deliberately lossy mode below.

## Identity and evidence boundary

| Item | Identity / status |
| --- | --- |
| Reported games | Batman Begins; Azurik: Rise of Perathia |
| Reported executable version | `0.8.136-402-g9c529ab612` |
| Reported host | Windows 11 25H2; Ryzen 7 5700X; RTX 3060 Ti; 16 GB RAM |
| Reported source | `9c529ab612ddef931f0ca63842a8a68c44c047f1` |
| Audited fork main | `134de6616e1d8f5bfe9d919a4e98e0ff7da3c928` |
| Audited main tree | `0e46fa61027022d0237eb9d484a3222c1b919cfc` |
| Review date | 2026-09-24 |
| New native game runs / product builds | Not performed |
| Attached `xemu.log` | Attachment identified, but its contents were not retrieved; no claim based on that log |

The [source comparison][comparison] puts audited main 34 commits after the report. It includes presentation, input, configuration, and arithmetic changes. Reproduce the reported executable and current main separately; do not silently use one as evidence for the other. Driver version, effective backend/mode, exact transition, and cache contents remain unverified.

## Decision on the default

Do not switch the default to Always as a presumed repair. First compare **Fallback versus Prewarm with identical cache seeds, explicit settings, and a process restart**. That isolates the extra learned-family preparation more closely than Off versus Prewarm, which changes several behaviors at once.

[Current configuration][settings] defaults `vk_ubershader_mode` to `prewarm` and shader fastpath to true, but the renderer default is **OpenGL**. Ubershaders require an operational Vulkan renderer. [Runtime state][mode] distinguishes requested, restart-latched policy, and effective mode; legacy saved choices are migrated rather than overwritten. Neither a new default nor a saved menu selection proves that a reported run actually used ubershaders.

| Mode | Meaning for this investigation |
| --- | --- |
| Off | Specialized rendering control. Useful for comparing the overall hybrid feature, not just prewarm scheduling. |
| Fallback | Reuse complete admitted fallback executables when available; retain demand-driven family preparation and specialization. Not a no-background-work mode. |
| Prewarm | Fallback routing plus bounded replay of learned families with available artifacts. Not a compile-everything-before-play barrier. |
| Always | Prefer the interpreter for supported draws and avoid ordinary specialization there. Current shader initialization also enables learned-family prewarm in this mode. Cold pipeline construction, unsupported-state escapes, and interpreter GPU cost still apply. |

The Always initialization detail is important: the earlier #142 description is not sufficient documentation of current main. A strict interpreter/no-speculative-prewarm comparison needs a separate diagnostic control or an explicitly empty, verified history; the current mode alone is not that control. [Shader initialization and preparation][shaders] are authoritative.

## Proposed Advanced option: continue with black frames

This is a design target, not an implemented setting or a proven stutter fix. Retain the current ubershader modes and add a separate **Shader miss handling** policy:

| Choice | Contract |
| --- | --- |
| Wait (default/reference) | Keep the existing accuracy-preserving behavior when no complete executable is available. |
| Continue with black frames (experimental) | Use a ready executable when the selected mode permits it. Otherwise capture and queue the missing executable, omit the unrenderable draw, and keep processing the guest stream without waiting for shader/PSO compilation. Suppress incomplete game output with host-side black presentation. |

Proposed help text: "Avoid waiting for missing shaders and pipelines. Gameplay, input and audio continue while the game view may turn black. Missing rendering can also affect game behavior. Compiled results are reused when available; other sources of stutter may remain."

The initial default stays Wait and explicit saved choices remain intact. Availability must follow actual renderer/worker support. This policy is conceptually separate from Off/Fallback/Prewarm/Always: do not relabel Always or silently change its interpreter preference. Qualify supported combinations; a combination without a functioning async path must be shown unavailable, not falsely reported active. Prewarm remains useful for reducing how often blackout is needed.

### Avoid the actual wait, not just the visible image

A literal fragment shader returning black still needs a compatible complete graphics pipeline and may need vertex/geometry processing. It is not a universal zero-cost escape from pipeline compilation. Vulkan pipeline objects include shader and fixed-function state ([pipeline specification][vk-pipelines]). Prefer omission plus an already-ready host presentation clear; do not compile a replacement black pipeline on the miss or erase the guest render target to hide it.

```text
Demand -> ready permitted specialized or ubershader executable -> render
       -> no ready executable
            Wait: existing required construction path
            Continue-black:
                capture immutable full recipe / deduplicate request
                queue missing stages and pipeline without waiting
                omit this draw; flag incomplete game output
                keep consuming commands and discovering further misses

Workers -> compile/load artifacts -> build complete pipeline
        -> validate generation/ticket -> renderer-owned publication
        -> later draws use the result; old skipped draws are not replayed

Host presentation -> incomplete output: black game view, responsive UI
                  -> current output recovered: resume game view
```

Do not wait for all global prewarm work to finish before restoring output. Base recovery on current-frame needs and affected resource dependencies. A short measured anti-flicker hold can group a burst, but a fixed multi-second blackout is not required. A frame with no new misses is not proof that a texture written by an earlier skipped pass has recovered.

### Concrete integration work

**Demand compilation cannot depend on a drawable fallback.** At audited main, `pgraph_vk_enqueue_specialized_fragment()` returns unless both fallback pipeline and resources are ready (`vk/shaders.c`). Add a separate owned demand-build path; simply removing the guard would not establish correct source, control or lifecycle ownership. Capture full shader/pipeline state before consuming the draw. Support missing vertex, geometry, fragment and pipeline stages as needed, including a valid specialized route when the interpreter rejects a state. Honor the selected mode's route rules.

**Branch before synchronous materialization.** `create_pipeline()` in `vk/draw.c` selects the uncovered route before activating shaders; insert explicit miss-policy handling before activation can create missing modules or the pipeline. The function currently binds textures first, so an early shader return alone does not remove texture/upload stalls. Audit ready-only probes and separate expensive materialization where measured. Worker source generation, cache-hit adoption, reflection, pipeline building and publication must not merely move the same long wait elsewhere on the render thread.

**Use an explicit omitted-draw outcome.** Propagate `READY / OMITTED / ERROR` semantics through callers rather than treating an ordinary failure return as a completed draw. Drain the guest command/state stream, retire per-draw buffers and references, maintain dirty-state obligations, and continue completion processing. Do not fabricate Vulkan handles, mark skipped GPU writes completed, replay stale draw inputs after the game advances, or delete synchronization needed by work that was actually submitted.

**Keep discovery and completion alive during blackout.** Missing draws must still discover/queue all currently encountered identities; skipping discovery would create a compile-one-shader-then-blackout-again loop. Service completions without requiring a successfully presented guest frame. Use demand priority, exact-key deduplication, bounded owned pending records, fair retries and CPU/memory limits. Queue saturation must defer or explicitly report lost admission, not silently invoke synchronous compilation or claim unqueued work is compiling.

**Separate host black presentation from guest rendering.** Gate the game image before expensive framebuffer acquisition/copy where possible, while retaining a responsive menu and a small compiling/failed status. Continue ready rendering and guest command consumption as required for discovery and dependencies. Handle reset, backend changes, shutdown and stale worker results. On a permanent build failure, report that it failed and let the user change policy; do not leave an indefinite fictitious "compiling" status or silently switch to blocking.

**Define the lossy consequences rather than prohibiting them.** Skipping a draw can omit depth/stencil changes, query contributions and render-to-texture content used later. The user accepts this tradeoff in this mode; it is not automatically only a cosmetic loss. Preserve the contracts for remaining operations and track affected resources. Rebuild or overwrite incomplete resources when possible; otherwise expose residual artifacts or gameplay differences in compatibility results. A one-time skipped texture-generation pass may not repair itself when compilation finishes. Accurate-path tests still require unchanged output, whereas lossy-mode tests document the omissions and recovery behavior.

**Cache completed work for future use.** Publish complete executables for later matching draws, retain appropriate demanded working sets, and reuse the existing validated SPIR-V/history/driver-cache lifecycle. Test repeat launches with identical cache seeds and clean shutdown. A persisted shader does not guarantee a resident pipeline on the next launch; keep loading/materialization nonblocking in the opt-in mode too. Missed draws are not a substitute for actually completing and persisting their compilation work.

### Separate acceptance criteria

The feature can be accepted as an explicitly lossy responsiveness option without claiming that accurate rendering in #198 is fixed. It does not need to wait until every non-shader hypothesis is eliminated, but it does need proof that it avoids targeted waits and makes forward progress.

Use production-linked tests for missing stages/PSOs with no fallback, duplicate requests, queue saturation, unsupported interpreter states, stale completions, omitted-draw cleanup, reset and shutdown. Add native tests where rendering is omitted, the guest continues, dependencies recover or their limits are recorded, and later cached runs need less omission.

Measure foreground shader-wait time, guest/input/audio progress, compiler contention, black-frame count and total/longest blackout, omitted draws, pending/failed/dropped work, time to usable complete pipelines and warm-run recovery. Do not count a smooth black UI or suppressed frames as rendered gameplay FPS. Pixel parity is required when the feature is disabled; temporary output differences are expected when enabled. Report any persistent visual/gameplay impact instead of excluding it from the results.

This changes the previous policy conclusion: [Epic's documented skip-draw/default-material strategies][epic] are legitimate inspiration for an opt-in performance mode. The emulator must still determine its own command, resource and recovery behavior; engine-owned component semantics cannot simply be assumed for guest draws.

## Source audit: identified limits and safeguards

These are code observations, **not measurements of Batman or Azurik**.

| Finding | Current path and implication |
| --- | --- |
| Prewarm requires learned full-family recipes | `pgraph_vk_hybrid_prewarm_prepare_record()` decodes a saved `PipelineKey`, validates device support, and probes complete readiness. No saved recipe means no anticipation of that family. [Preparation][prepare] |
| Missing shader artifacts are not compiled by prewarm | Cached vertex, optional geometry, and fragment stages must be available. `MISSING_ARTIFACT` is marked attempted by this service, rather than waiting for a later artifact arrival. Demand-driven learning may still recover separately. [Policy][prewarm] |
| Startup is not gated on prewarm completion | `pgraph_vk_flip_stall()` runs family service and prewarm after `pgraph_vk_finish()`. Renderer initialization does not drain a complete fallback set before play. Few flips or early demand can beat preparation. [Renderer][renderer] |
| The cap is 32 candidates per launch | Missing/rejected attempts consume the allowance as well as successful submissions. This is not 32 guaranteed useful pipelines and not a time budget. [Limit][prewarm-header], [policy][prewarm] |
| Broad busy gating can prevent progress | `hybrid_demand_work_waiting()` includes any retained family request and any in-use pipeline job, including a prior prewarm job. Investigate starvation and effective serialization; do not assume more worker threads will fix it. [Draw orchestration][draw] |
| An empty eligible set can disable the service | When considered equals attempted and no candidate exists, `enabled` becomes false. Investigate whether newly learned or newly materializable work needs an explicit, bounded reactivation event. [Policy][prewarm] |
| Some preparation remains on the owner thread | Cache-only preparation still generates/looks up sources, materializes modules, prepares bindings and builds recipes before worker submission. One attempt per flip does not bound the duration of one attempt. [Shaders][shaders], [draw][draw] |
| The interpreter is not the entire NV2A pipeline | `psh-uber.c` interprets combiners using supplied texture results. Vertex programs, texture-shell variants, geometry, and fixed-function graphics-pipeline identity can still require other executables. [Interpreter][uber], [draw][draw] |
| Full-executable promotion already exists | `specialized_complete` requires both shader binding and graphics pipeline. The outer draw selector owns execution; the older selector in `shaders.c` is also used for requesting specialization. Do not diagnose module-only takeover from that inner function alone. [Draw selector][draw] |
| Driver pipeline persistence already exists | `pipeline_cache_read/save()` qualifies data with vendor/device/cache UUID and supplies it to `vkCreatePipelineCache`. Cached SPIR-V and learned history are not the only stores. A valid disk cache is still not a live ready `VkPipeline`. [Cache lifecycle][draw] |
| Resource-induced waits remain possible | Pipeline-cache exhaustion and descriptor/uniform capacity paths can call `pgraph_vk_finish(...NEED_BUFFER_SPACE)`. Existing control-only uploads can reuse descriptors; do not reintroduce that optimization as a new fix. [Draw][draw], [descriptors][shaders] |
| Existing statistics do not attribute each hitch | Family summary records preparation time and first-demand hits; hybrid tracing records uncovered routes. A hit count without a first-demand denominator or timestamps cannot prove coverage or explain a particular spike. [Shaders][shaders], [renderer][renderer] |

[PR #144][pr144] explicitly limited prewarm to opportunistic learned-family preparation. Its earlier 12-first-demand-hit smoke is useful historical evidence, not broad loading-stall qualification. [PR #150][pr150] is merged even though its retained description says draft; it did not establish an isolated performance benefit for the new default. Audit live code and metadata rather than treating old PR wording as the current implementation.

### Processing difference to investigate

```text
Current learned-family path
  restore history and caches
    -> wait for a flip and no outstanding hybrid work
    -> consider up to 32 families across this launch
    -> cached stages only; missing artifact consumes this attempt
    -> owner preparation -> worker pipeline creation -> owner publication

Current demand path
  complete specialized executable -> draw
  otherwise complete admitted fallback -> draw + request specialization
  otherwise uncovered route -> synchronous construction can block the draw

Accuracy-preserving target, not yet implemented
  first demand -> a correct complete executable is already available
  missed prediction / unsupported state -> explicit reason and preserved output
  specialization -> background work -> complete publication -> later promotion

Opt-in performance target, not yet implemented
  missing executable -> queue work -> omit draw / black presentation
  guest processing continues -> publish executable -> recover later output
```

Prewarming more bytecode cannot establish that target property when the missing item is a vertex variant, a different full pipeline, or a stalled publication. Likewise, even complete shader coverage cannot remove guest I/O or presentation waits.

## Elimination matrix

Every row starts **OPEN for the reported games**. The source audit rules out absent mechanisms where noted above; it does not rule out their failure or poor timing in a particular run. Close a row only with the event interval, exact build identity, counter/trace evidence, and an appropriate negative control. Multiple causes may coexist.

| ID / hypothesis | Discriminating measurement and test | Evidence needed to eliminate or act |
| --- | --- | --- |
| H01: Wrong effective configuration | Record requested/policy/effective mode, installed backend, restart flag, cache eligibility, selected GPU, shader debug/validation, accurate arithmetic and other overrides. | Eliminate configuration mismatch only when effective Vulkan/mode and controlled options are confirmed throughout the reproduction. |
| H02: Cold, absent, rejected, or unwritten caches | Independently inventory history, SPIR-V, application pipeline cache, and resident objects; check clean-shutdown writes versus forced exit. | Identify which layer missed. Loaded bytes or SPIR-V hits alone do not eliminate pipeline creation. Driver-internal cache state remains unknown unless independently controlled. |
| H03: First-family or uncovered-stage compilation | Attribute each uncovered draw to vertex, geometry, fragment shell/combiner, or complete pipeline; retain normalized identities and rejection reason. | Broaden only the coverage responsible for measured blocking. If no compilation overlaps a hitch, investigate another row. |
| H04: Unsupported combiner/state admission | Count packer rejection reasons and affected first demands; compare supported and rejected workload states. | Eliminate only with zero relevant rejects. Do not admit encodings or approximate arithmetic without oracle/GPU and, when applicable, hardware evidence. |
| H05: Scheduling too late, starvation, or cap exhaustion | Record service opportunities, busy reasons, candidate rank, 32-candidate exhaustion, queue/start/finish/publish/first-demand times. | A missing candidate and a queued-but-late candidate require different repairs. Test fairness independently of changing worker count. |
| H06: Missing-artifact terminal attempts or disabled service | Test empty history, absent stages that later arrive, candidate deferral/exhaustion, and a later newly learned family. | Prove whether a valid later opportunity is lost. Add bounded event-driven retry/reactivation only for a demonstrated lost opportunity. |
| H07: Owner-thread preparation or cache-hit materialization | Separate GLSL generation, hashing, artifact adoption, reflection, module/layout/render-pass preparation, and publication time from worker time. | Move measured expensive CPU work behind immutable owned inputs; keep renderer-owned state mutation and required synchronization correct. |
| H08: Driver creation, internal serialization, or background contention | Timestamp `vkCreateGraphicsPipelines`, queue delay and foreground waits; compare identical recipes with controlled speculative scheduling. Use creation feedback when supported. | Distinguish slow creation from waiting to start. Shared-cache use alone is not proof of a Vulkan synchronization violation; do not blindly add a global mutex. |
| H09: Duplicate in-flight builds | Detect overlapping same-full-key source/pipeline requests when demand arrives before a worker publishes. | Prove duplicate driver work before deduplicating. With no executable available, merely waiting on the existing job still stalls; early readiness remains necessary. |
| H10: Completed but unpublished work | Track worker completion, validation, safe cache-slot admission, publication, and first use; inspect renderer-lock and service delays. | Separate worker lateness from publication lateness. Never publish stale generation/ticket results or evict an in-flight object to improve a counter. |
| H11: Key explosion, history pollution, eviction | Compare full-key count with per-stage source count; record key-field differences, eviction/recreation, family age and usefulness by workload. | Normalize only state proven irrelevant to the actual shader/pipeline. Test cross-title histories and the working set before raising limits or pinning objects. |
| H12: Interpreter GPU cost or pipeline switching | Use GPU timestamps/captures and a warm, supported-state interpreter comparison at fixed resolution; separately inspect route transitions. | If CPU compile time is absent but GPU time grows, optimize interpreter execution/switching rather than prewarm. Account for Always's current prewarm behavior. |
| H13: Descriptor, control, uniform, vertex staging exhaustion | Correlate capacity counters and `NEED_BUFFER_SPACE` finish reasons with hitches. | A capacity wait is not a shader miss. Repair lifetime/retirement or measured capacity pressure without weakening visibility or reuse guarantees. |
| H14: Textures, surfaces, readbacks, queries or VRAM pressure | Attribute uploads, decoding/unswizzling, transfers, dirty-surface downloads, report/query waits and memory residency around the same frame. | Use correct warm-resource controls. Do not call omitted work an accuracy-preserving pass. The opt-in mode separately records omissions, dependent-resource effects and any changed query/gameplay outcomes. |
| H15: CPU emulation or guest loading | Capture vCPU/PFIFO/PGRAPH timelines, TCG translation, storage activity, decompression/audio work and lock waits. | Continued stalls with ready pipelines and no shader work keep this branch open; a low total CPU percentage does not exclude a saturated emulation thread. |
| H16: Presentation, frame pacing or transport | Correlate guest display-write progress with output presentation, present waits, VSync and shared-memory versus host-copy transport. | Stable guest progress with late presentation is not a shader-compilation result. Separate reported-build and current-main presentation changes. |
| H17: Host/driver conditions | Record driver, clocks, memory pressure, background load and scheduling interruptions; repeat controlled same-scene runs and a second vendor where available. | Do not label a vendor defect from one machine or from another emulator's warning. Eliminate host interference with repeatable evidence. |
| H18: Measurement artifact | Verify capture starts before loading, trace capacity/drops, exact replay, marker validity, instrumentation overhead and cold/warm ordering. | Missing events do not prove absence when the trace was exhausted or started too late. Never substitute UI presentation rate for guest progress. |

## Shared instrumentation for accurate repair and opt-in omission

Extend existing diagnostics rather than building a second logging framework. Relevant entry points are `pgraph_vk_resolve_ready_execution_candidates()`, `pgraph_vk_hybrid_choose_uncovered_route()`, `request_hybrid_pipeline()`, `pgraph_vk_process_hybrid_pipeline_completions()`, `pgraph_vk_process_hybrid_prewarm()` and the cached-module materialization path in `shaders.c`.

Add a bounded first-demand record connecting **family/key -> request -> worker -> publication -> use**. Preserve existing generation/ticket ownership. Proposed data contract, not an existing emitted format:

```text
frame_id, monotonic_us, generation, ticket
pipeline_hash, family_hash, vertex_source_hash, fragment_source_hash
requested_mode, effective_mode, selected_route, admission_reason
history_found, stage_artifact_mask, full_pipeline_ready
prewarm_outcome, busy_reason, candidate_rank
queued_us, started_us, finished_us, published_us, first_demand_us
owner_prepare_us, synchronous_create_us, resource_wait_us
classification = READY | MISSED | TOO_LATE | UNSUPPORTED | RESOURCE_WAIT | OTHER
```

Define first demand by exact canonical family/pipeline identity and generation, not by a shader module alone. Keep separate totals for demanded families, admissible demands, ready-before-demand hits, unneeded prewarms, evictions before first use, and time blocked. Report `ready_before_demand / admissible_first_demands`; also disclose unsupported demands rather than hiding them from the overall result. An aggregate `ready` count is not that ratio.

Record expensive events and per-frame aggregates, with bounded memory and explicit dropped-event accounting. No unconditional per-draw file output, driver queries, large hashes, or timestamps in normal operation. Diagnostic overhead must be measured separately from acceptance timing.

Existing controls worth reusing are `XEMU_VK_HYBRID_TRACE` and `XEMU_VK_PIPELINE_CACHE_LOG=1`. The current trace is opened with a 50,000-event bound; verify that the loading interval is retained. Enable diagnostics only for separate attribution captures and remove them for primary performance runs. Redact machine paths and identifiers before publishing logs.

Optional Vulkan creation feedback/cache-control experiments must query feature support and preserve the ordinary path. A compile-required result selects an already-ready correct fallback or required blocking in Wait mode. In Continue-black mode it can select an explicitly recorded omission and owned async build; never bind an invalid pipeline or treat that omission as accurate rendering. Absence of an application-cache-hit flag does not describe every driver-internal cache.

## External implementations: what transfers and what does not

These are primary project/vendor publications. Epic, Unity and Valve provide commercial-engine/tooling comparisons; they are not claims of access to proprietary code or evidence that every listed product uses an emulator-style ubershader.

| Project | Relevant published behavior | Application to this fork |
| --- | --- | --- |
| [Dolphin][dolphin] | Hybrid interpretation covers missing specialized shaders; its compile-before-start option prepares cached identities before execution. Its performance guide also warns of NVIDIA/Vulkan hybrid switching stutter. | Separate broad fallback coverage, readiness before first demand, and driver switching. The warning motivates a test on the reporter's RTX, not a diagnosis or an automatic backend change. |
| [DXVK 3.0][dxvk3] / [fixed-function implementation][dxvk5192] | D3D8/9 fixed-function handling uses vertex/pixel ubershaders with optimized variants prepared in the background. Shader translation itself also moved off the application thread in 3.0. | Study missing vertex/fragment-shell coverage and owner-thread translation, not only fragment-combiner compilation. NV2A semantics still need independent validation; do not transplant D3D9 behavior blindly. |
| [Epic Unreal Engine][epic] | Tracks shader-only, partial and full PSO readiness, including missed versus too-late work; recommends waiting for outstanding precompiles during loading and budgeting compilation resources. | Adopt deadline/coverage attribution and consider an explicit preparation phase. Its skip-draw/default-material strategies also motivate the owner-approved opt-in performance mode; distinguish that mode from accurate emulation. |
| [Unity warmup warning][unity-warning] / [graphics-state warmup][unity-warmup] | On Vulkan, different vertex layouts or render targets can require more driver work despite shader warmup. Graphics-state capture provides a more exact warmup input. | Keep shader artifacts distinct from complete pipeline recipes and live executables. Verify full-state equality at replay and demand. |
| [Valve Fossilize][fossilize] | Captures and replays Vulkan object dependencies, including pipelines, allowing focused compiler replay independent of the full application. | Capture a slow, valid complete recipe to distinguish driver compilation from guest loading. Validate feature/device requirements; do not distribute opaque driver caches as universal executables. |
| [Khronos graphics pipeline libraries][gpl] / [DXVK's adoption][dxvk2] | Pipeline parts can be built separately and linked into an executable; early compilation depends on when inputs become known. | Prototype only after full-PSO creation is measured as dominant. Check fast-link/optimization tradeoffs and support; retain the monolithic reference path. This does not remove all possible first-use work. |

The common lesson is **a correct usable executable before demand**, plus measured coverage and scheduling. Merely increasing a cache-hit counter or worker count does not establish that property.

## Ordered repair plan and files to touch

### A. Establish accurate-path causes and the opt-in miss boundary

Start with H01/H02, then instrument the first reproduced hitch. In parallel, prototype the owner-approved Continue-black behavior at the uncovered executable boundary; do not gate this feature on proving all 18 hypotheses false. Use the existing unit registration in `tests/unit/meson.build` to locate prewarm/history/runtime fixtures; do not rely on obsolete test counts from an earlier PR. Maintain observations and experiment decisions on #198, candidate results in the draft PR, and compact manifests/raw tables under `evidence/wiki-xiso-per-test/pr-<number>/issue-198/`. Promote durable conclusions to the wiki after qualification instead of copying evolving narratives across multiple PRs.

### B. Repair a demonstrated preparation-lifecycle gap

Primary files: `vk/hybrid-prewarm.{c,h}`, `vk/hybrid-prewarm-runtime.c`, `vk/draw.c`, `vk/renderer.c`, and the history scheduler when relevant.

Develop production-linked regressions for empty-start/later-learning, artifact-arrival retry, a useful family beyond rank 32, deferred head-of-line work, and worker-ready-but-unpublished states. Separate missing, waiting, attempted and permanently rejected outcomes. Use event-driven bounded retries and measured CPU/memory admission; do not simply remove caps or turn every artifact miss into synchronous compilation.

A wall-clock service budget can stop admitting subsequent work, but cannot preempt one slow driver call. Long operations need the appropriate worker/owned-input boundary, not a nominal per-flip budget that still blocks for one oversized task. Avoid querying live mutable guest state from worker threads. Preserve safe cancellation, cache retirement, renderer reset and shutdown.

### C. Make preparation before play explicit, if evidence supports it

Primary files: `vk/renderer.c`, `vk/shaders.c`, `vk/draw.c`; UI/persistence changes in `ui/xemu-tweaks.{c,h}`, `config_spec.yml` and `ui/xui/main-menu.cc` only as a separately qualified policy change.

Prototype an opt-in pre-start preparation phase for validated known recipes, with progress, cancellation and honest remaining-work reporting. It can help repeat launches, but a learned-only queue cannot promise cold-first-launch coverage. Broad cold coverage requires identifying a bounded general fallback set and its complete executable variants, not waiting forever on history that does not exist. Do not stall guest execution while holding locks needed to publish completion. Report added launch time separately from reduced interactive hitches.

### D. Expand coverage only where the traces justify it

Primary files: `glsl/psh-uber.c`, the surrounding pixel/vertex generators, `vk/shaders.c`, `vk/draw.c`, and control admission/codec code where the contract changes.

Measure which key dimensions create uncovered families. Investigate a more general texture shell, vertex interpretation or smaller fallback families only for high-impact dimensions. Audit generated-source and full-pipeline dependencies before canonicalizing fields. Already-removed uniform-only permutations and complete-pipeline promotion must remain intact. Any new admitted semantics need CPU/reference, generated GLSL and GPU comparison; use focused XBEs against real Xbox hardware when changing emulation behavior.

### E. Driver/PSO and resource work remains separate

If H08 dominates, prototype graphics pipeline libraries, supported dynamic state, or a measured cache/worker topology experiment separately. If H12-H17 dominate, repair that subsystem instead of relabeling the problem an ubershader miss. Do not combine a new interpreter, cache format, scheduling rewrite and default flip into one unreviewable performance patch.

## Required test campaign

Use exact optimized Release artifacts with compiler options, LTO/ISA, source/tree SHA, executable SHA-256 and matching symbols. Retain the reported build if available, current-main control, candidate with the changed feature disabled, and candidate enabled. The older fixed baseline is a separate cumulative reference, not a substitute for contemporary main.

First obtain a deterministic full-boot/loading transition for both reported games. Include first entry, level restart, and a transition introducing new states; snapshots alone can hide the cold path. Retain PGR2 and Morrowind as regression controls and run the maintained XISO correctness suite.

| Axis | Required distinctions |
| --- | --- |
| Mode | Explicit Off, Fallback, Prewarm and diagnostic Always, with restart and effective-state proof; isolate prewarm scheduling further when necessary. |
| Cache | Empty application history/artifacts; learned history plus warm SPIR-V; restored application pipeline cache; same-process reuse; a new state after warmup. Declare driver-internal cache state independently as controlled or unknown. |
| Repetition | Repeated matched runs with bracketed/reversed order, such as ABBA then BAAB. Restore identical private cache/profile seeds for each corresponding role. |
| Hardware | Start with the reported RTX 3060 Ti/Windows configuration when available, then a second NVIDIA/driver condition and AMD/Intel coverage appropriate to the change. Never infer portable behavior from one driver. |
| Lifecycle | Clean exit/writeback, interrupted exit, renderer recreation, stale completion, cache corruption, cache pressure and shutdown while work is pending. |
| Timing | Separate attribution captures from uninstrumented acceptance runs. Include load-to-playable time and the entire troublesome transition. |

Record guest display-write intervals and host presentation separately, p95/p99/p99.9/max, hitch counts and total stall time above declared thresholds (for example 50/75/100 ms), ready-before-demand coverage and CPU/GPU time. Keep raw timelines: a good average or a long capture's p99 can conceal one severe loading stall. Thresholds must be tied to the workload's intended cadence, not treated as proof that every 33 ms interval is a defect.

An accuracy-preserving repair needs reproduced reduction of the attributed stalls, unchanged required output/side effects, and no repeatable greater-than-2% regression against contemporary main in the accepted comparable metrics. The opt-in Continue-black feature is qualified separately on responsiveness, explicit omissions, recovery and cache reuse; unchanged output during blackout is not its gate. Its disabled/reference path must retain correctness and avoid a repeatable greater-than-2% regression in accepted comparable metrics. Report uncertainty and order drift; do not treat a single maximum as statistically stable. Preserve framebuffer checks and explicit inspection of affected effects, depth/query behavior and scene completeness. Skipped draws or blank output may be successful results for the explicitly selected performance mode; report their cost and do not relabel them an accuracy-preserving fix. Blanket effect suppression beyond shader misses is outside this feature.

## Current result and next decision

**Completed:** pinned-source audit, comparison with the reported revision, review of related prewarm/default work, primary-source comparison, an investigation plan, and the owner-directed opt-in blackout design. The blanket exclusion of deliberately incomplete rendering has been removed.

**Not completed:** retrieval of the attached log, native reproduction, trace attribution, a renderer patch, product compilation, GPU correctness qualification, or measured performance improvement. No measured results or fabricated CSV rows are included in this draft.

The accurate-path repair is driven by the first correlated hitch: **missed coverage, too-late readiness, owner/driver contention, resource wait, or non-shader work**. The opt-in Continue-black feature is a separate accepted path for users who prefer omitted rendering over shader waits. Keep existing defaults unchanged in this documentation draft. Keep #198 open until the reported games and chosen modes are qualified, including blackout/recovery tradeoffs and residual non-shader causes.

## Sources

[issue]: https://github.com/Mainkill1/xemu/issues/198
[comparison]: https://github.com/Mainkill1/xemu/compare/9c529ab612ddef931f0ca63842a8a68c44c047f1...134de6616e1d8f5bfe9d919a4e98e0ff7da3c928
[settings]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/config_spec.yml
[mode]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/ui/xemu-tweaks.c
[prewarm]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/hybrid-prewarm.c
[prewarm-header]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/hybrid-prewarm.h
[prepare]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/hybrid-prewarm-runtime.c
[draw]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/draw.c
[renderer]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/renderer.c
[shaders]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/shaders.c
[uber]: https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/glsl/psh-uber.c
[pr144]: https://github.com/Mainkill1/xemu/pull/144
[pr150]: https://github.com/Mainkill1/xemu/pull/150
[dolphin]: https://dolphin-emu.org/docs/guides/performance-guide/
[dxvk3]: https://github.com/doitsujin/dxvk/releases/tag/v3.0
[dxvk5192]: https://github.com/doitsujin/dxvk/pull/5192
[epic]: https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine
[unity-warning]: https://docs.unity3d.com/6000.0/Documentation/ScriptReference/ShaderVariantCollection.WarmUp.html
[unity-warmup]: https://docs.unity3d.com/6000.0/Documentation/Manual/shader-prewarm.html
[fossilize]: https://github.com/ValveSoftware/Fossilize
[gpl]: https://docs.vulkan.org/samples/latest/samples/extensions/graphics_pipeline_library/README.html
[dxvk2]: https://github.com/doitsujin/dxvk/releases/tag/v2.0

[vk-pipelines]: https://docs.vulkan.org/spec/latest/chapters/pipelines.html
