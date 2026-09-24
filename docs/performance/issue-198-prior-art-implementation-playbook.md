# Issue #198 / PR #203 Prior-Art Implementation Playbook

> Required reading before implementing the shader-stall work tracked by issue #198 and PR #203.
>
> The goal is to adapt proven emulator, engine, translation-layer, and Vulkan techniques instead of independently rebuilding them under different names.

## Executive correction

The first #203 investigation correctly identified incomplete executable readiness, learned-only prewarm limits, owner-thread work, and the need for an optional nonblocking miss policy. The deeper prior-art pass changes the implementation order:

1. **Do not treat Continue/blackout as a novel renderer design.** Cemu already ships the core model: attempt a compile-forbidden Vulkan pipeline create, queue missing work asynchronously, and skip unavailable draws when the user accepts temporary rendering loss.
2. **Do not assume async means foreground-safe.** DXVK moved shader translation itself off the application thread, not only final pipeline creation.
3. **Do not treat every PipelineKey as inherently expensive and indivisible.** Dolphin reduces shader/pipeline identity entropy; Unreal starts from shader-only/minimal PSOs; Vulkan GPL explicitly supports reusable pipeline parts and fast-link-before-LTO.
4. **Do not classify every first-use failure as a cache miss.** Unreal's Too late category separates a correctly predicted job that missed its deadline from an undiscovered state.
5. **Do not assume a disk/driver cache hit means a ready low-latency executable.** Unreal documents driver-cache reconstruction cost and can keep precached PSOs resident until first use.
6. **Do not lower every compiler thread unconditionally.** Cemu deliberately keeps one pipeline compiler at normal priority and lowers the remainder to avoid starving all compilation.
7. **Do not limit fallback readiness to fragment-combiner state.** RPCS3's interpreter work demonstrates that format/output-dependent fallback variants can still produce catastrophic first-use compilation if precompile coverage is incomplete.

The target is therefore not simply:

~~~text
miss -> async compile -> ubershader/black
~~~

It is:

~~~text
guest state
    -> canonical/minimal identity
    -> already-ready specialized executable?
    -> already-ready fallback executable?
    -> driver compile-forbidden exact-pipeline probe
    -> reusable GPL fast-link candidate
    -> retained exact demand build
    -> optional draw omission / black presentation
    -> background optimized replacement
    -> persist useful identities/artifacts for next launch
~~~

## Open research lanes

These issues are deliberately separated so #203 does not become an unreviewable renderer rewrite.

| Issue | Lane | Relationship to #203 |
| --- | --- | --- |
| #204 | Compile-required foreground probing | Strong candidate prerequisite for Continue. Driver-authoritative ready-without-compile check. |
| #205 | Graphics Pipeline Library fast-link + background LTO | Experimental alternate pipeline-construction architecture. |
| #206 | Pipeline-key entropy + minimal precompile recipes | Reduces work before adding more workers/caches. |
| #207 | Fully off-thread shader translation + demand promotion | Moves source generation/translation behind immutable worker recipes. |
| #208 | Hot-ready PSO retention, module identifiers, pipeline binaries | Warm-run/cache optimization lane; not required for first Continue version. |
| #174 | Selective specialization constants | Existing related shader-variant experiment. Keep separate from key/GPL work. |
| #97 | Shader-cache policy | Existing cache policy owner. Do not create a second competing cache-mode framework. |

Implementation PRs should link both their owning issue and #203 so the global goal remains visible.

---

# 1. Cemu: Continue mode already exists in production form

## What Cemu does

Cemu's Vulkan async compilation is the closest direct prior art to the proposed xemu Continue policy.

Cemu documents that when async compilation is enabled:

- uncached shaders and pipelines compile asynchronously;
- draw calls whose pipeline is still compiling are skipped;
- temporary missing/broken graphics are accepted;
- some GPU work cannot safely be skipped, so Cemu uses speculative logic to identify essential shaders that must remain synchronous;
- the feature requires pipeline creation cache control.

References:

- Cemu 1.19.0 async behavior:
  https://wiki.cemu.info/wiki/Release_1.19.0
- Current Cemu implementation:
  https://github.com/cemu-project/Cemu/blob/5e09ec72a43dc8e857f94619d3b0a40c2bd65298/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.cpp

The important implementation is not merely queue compilation. Cemu asks the Vulkan driver whether the pipeline can be produced **without compilation**:

~~~cpp
if (!forceCompile)
    pipelineInfo.flags |=
        VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT;

result = vkCreateGraphicsPipelines(...);

if (result == VK_ERROR_PIPELINE_COMPILE_REQUIRED_EXT)
    return false;
~~~

The worker later calls the same construction path with compilation allowed.

## xemu adaptation

This should become the preferred Continue control flow rather than inferring readiness solely from xemu cache bookkeeping:

~~~c
typedef enum PGRAPHVkPipelineProbeResult {
    PGRAPH_VK_PIPELINE_PROBE_READY,
    PGRAPH_VK_PIPELINE_PROBE_COMPILE_REQUIRED,
    PGRAPH_VK_PIPELINE_PROBE_ERROR,
} PGRAPHVkPipelineProbeResult;
~~~

~~~text
exact PipelineKey
      |
      v
compile-forbidden driver probe
      |
      +-- READY
      |      -> publish/use exact pipeline immediately
      |
      +-- COMPILE_REQUIRED
      |      -> retain exact recipe
      |      -> queue background pipeline build
      |      -> ready fallback? use it
      |      -> Continue? omit draw / present black
      |      -> Wait? blocking reference construction
      |
      +-- ERROR
             -> real renderer failure path
~~~

This provides a stronger guarantee than:

~~~text
our cache does not contain it
-> maybe driver cache has it
-> call vkCreateGraphicsPipelines
-> unexpectedly stall anyway
~~~

Issue #204 owns this experiment.

## Essential/unsafe-to-skip work

Cemu also demonstrates that skip every unavailable draw is not generally safe. Wii U compute-like/GPGPU usage means some draw calls have durable side effects and must not be silently removed.

NV2A has different semantics, but xemu must explicitly classify omitted work.

At minimum, investigate whether the draw participates in:

~~~text
guest-visible color/depth result later read as texture/data
occlusion/query/report production
surface readback dependency
render-to-texture consumed before natural redraw
other guest-visible ordering/side effect
~~~

The first Continue implementation must remain conservative. Do not invent a complex heuristic before traces exist, but do not assume every ordinary-looking graphics draw is safely disposable either.

Potential policy surface:

~~~c
typedef enum PGRAPHVkOmissionSafety {
    PGRAPH_VK_OMIT_SAFE_TRANSIENT,
    PGRAPH_VK_OMIT_UNSAFE_QUERY_OR_REPORT,
    PGRAPH_VK_OMIT_UNSAFE_RESOURCE_DEPENDENCY,
    PGRAPH_VK_OMIT_UNKNOWN,
} PGRAPHVkOmissionSafety;
~~~

Continue may omit only a class whose behavior we have deliberately accepted/tested. Unknown can remain Wait initially.

This is an adaptation of a known emulator problem, not a reason to abandon Continue.

---

# 2. Cemu worker scheduling: one normal-priority compiler can prevent starvation

The current #203 worker-priority addendum initially assumed asynchronous shader and pipeline workers should all run at reduced host priority.

Cemu has a useful counterexample. Its Windows pipeline compiler pool lowers every compiler thread **except one**:

~~~cpp
if (threadIndex != 0)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
~~~

The code comment states why: protect main/render threads without creating the opposite scenario where all compiler threads are starved.

Cemu also sizes its pool from physical core count and caps it.

### Revised xemu experiment

Do not hard-code one answer before profiling. Test these configurations:

~~~text
A. current scheduling
B. all async workers lower priority
C. one normal-priority demand worker + lower-priority prewarm/speculative workers
D. demand job promotion to normal lane when first requested
~~~

Jobs should carry urgency:

~~~c
typedef enum PGRAPHVkCompileUrgency {
    PGRAPH_VK_COMPILE_SPECULATIVE,
    PGRAPH_VK_COMPILE_PREWARM,
    PGRAPH_VK_COMPILE_DEMAND,
} PGRAPHVkCompileUrgency;
~~~

Queue semantics:

~~~text
DEMAND > PREWARM > SPECULATIVE
~~~

An existing speculative/prewarm request should be **promoted**, not duplicated:

~~~c
if (exact_job_exists(key)) {
    job->urgency = MAX(job->urgency, new_urgency);
    job->deadline_hint_us =
        min_nonzero(job->deadline_hint_us, new_deadline_us);
    reprioritize(job);
    return DUPLICATE_PROMOTED;
}
~~~

Issue #207 owns this worker/scheduling direction.

---

# 3. Dolphin: separate skip-render, ubershader, pipeline identities, and startup/runtime workers

References:

- Pipeline-selection behavior:
  https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexManagerBase.cpp
- Shader/pipeline cache and workers:
  https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/ShaderCache.cpp
- Ubershader design background:
  https://dolphin-emu.org/blog/2017/07/30/ubershaders/

Dolphin distinguishes:

~~~text
Synchronous
SynchronousUberShaders
AsynchronousUberShaders
AsynchronousSkipRendering
~~~

That supports keeping the **fallback shader policy** and **miss behavior policy** separate.

For xemu:

~~~text
Ubershader policy:
    Off / Fallback / Prewarm / Always

Miss policy:
    Wait / Continue

These are orthogonal.
~~~

## Different worker configurations before and during gameplay

Dolphin uses a precompiler thread count while loading known cache entries, optionally waits for the compiler, then resizes to a runtime worker count.

This suggests xemu should not assume one worker count is ideal for every phase:

~~~text
startup/history replay:
    wider low-priority pool
    finite known work set
    optional user-visible wait only if explicitly selected

gameplay:
    smaller pool
    protect vCPU/PFIFO/APU/input
    demand promotion
~~~

## Key cleanup before compilation

Dolphin's UID/pipeline-cache architecture reinforces a key rule:

> Make the identity smaller before making the compiler faster.

For every member of xemu's ShaderModuleCacheKey, ShaderBindingKey and PipelineKey, classify it as:

~~~text
generated shader code
shader ABI/layout
static Vulkan pipeline state
dynamic-state candidate
uniform/control data
irrelevant in this route
~~~

Issue #206 owns the systematic entropy report and minimal-recipe work.

---

# 4. RPCS3: precompile the fallback itself, including host/output-dependent variants

References:

- Precompile interpreter shaders at boot:
  https://github.com/RPCS3/rpcs3/issues/17897
- Shader recompilation UX / Async with Shader Interpreter:
  https://github.com/rpcs3/rpcs3/issues/18219
- AMD Windows interpreter stutter caused by format-dependent variants:
  https://github.com/RPCS3/rpcs3/issues/19085

RPCS3's direction reinforces that a shader interpreter/ubershader is useful only if its own executable variants are ready before the draw that needs them.

The AMD Windows case is especially relevant: format-dependent export requirements meant interpreter precompile coverage missed variants; compiling those variants during gameplay could freeze for more than ten seconds.

### xemu lesson

Fallback readiness must describe a **complete executable family**, not merely that the combiner interpreter module exists.

Relevant dimensions may include:

~~~text
vertex shader/interface
geometry presence
fragment interpreter shell
texture/input shell state
render-target format
depth/stencil format
sample/output state
pipeline layout
render pass compatibility
~~~

The exact list is xemu-specific and must come from PipelineKey/module identity, but the requirement is:

~~~c
bool fallback_complete =
    vertex_ready &&
    (!needs_geometry || geometry_ready) &&
    uber_fragment_ready &&
    exact_pipeline_ready &&
    resources_ready;
~~~

A fallback that still invokes expensive first-use compilation is not a reliable fallback.

Prewarm metrics should report separately:

~~~text
fallback module ready
fallback binding ready
fallback full pipeline ready
fallback ready before first demand
~~~

---

# 5. Unreal Engine: Shader-only -> Minimal PSO -> Full PSO

Reference:

https://dev.epicgames.com/documentation/unreal-engine/pso-precaching-for-unreal-engine

Unreal's PSO precacher tracks three useful readiness levels:

~~~text
Shader-only PSO
Minimal PSO
Full PSO
~~~

A Minimal PSO contains shaders, render state and vertex information but can exclude final render-target information that is not known until draw time.

That suggests xemu should not wait for a complete PipelineKey before beginning expensive preparation.

## Proposed minimal precompile identity

Conceptual only:

~~~c
typedef struct PGRAPHVkPrecompileKey {
    ShaderState shader_state;
    uint64_t vertex_interface_hash;
    uint64_t descriptor_layout_hash;
    uint32_t raster_class;
    uint32_t depth_stencil_class;
} PGRAPHVkPrecompileKey;
~~~

Flow:

~~~text
state becomes predictable
    -> minimal precompile key
    -> generate/compile VSH/GS/PSH work
    -> materialize reusable module/binding prerequisites

final output/render-pass state appears
    -> exact PipelineKey
    -> compile-required probe
    -> GPL fast-link or ordinary background pipeline build
~~~

The Minimal PSO is **not** permission to bind an approximate pipeline. It is an early work identity.

Issue #206 owns this design.

---

# 6. Unreal: Missed and Too Late are different bugs

Unreal tracks at least:

~~~text
Hit
Missed
Too late
Untracked
Precached
~~~

The distinction matters enormously for #198.

### xemu vocabulary

~~~c
typedef enum PGRAPHVkReadinessClass {
    PGRAPH_VK_READY_HIT,
    PGRAPH_VK_READY_MISSED,
    PGRAPH_VK_READY_TOO_LATE,
    PGRAPH_VK_READY_UNSUPPORTED,
    PGRAPH_VK_READY_QUEUE_DEFERRED,
} PGRAPHVkReadinessClass;
~~~

Definitions:

- **HIT**: exact usable executable existed before first demand.
- **MISSED**: xemu had not predicted/queued the required executable.
- **TOO_LATE**: exact work was predicted and queued but not published by first demand.
- **UNSUPPORTED**: route could not represent the guest state.
- **QUEUE_DEFERRED**: work was known but bounded capacity prevented admission.

This prevents diagnosing a scheduling/deadline failure as a discovery/cache miss.

---

# 7. Unreal: keep precached pipelines resident until first use

Unreal documents that reconstructing a PSO from driver cache can itself cost milliseconds on some IHVs. It therefore has an option to keep a bounded number of precached PSOs in memory until they are actually used.

Before adding another disk cache, measure whether xemu:

~~~text
prewarms pipeline
-> inserts into LRU
-> never draws it immediately
-> evicts it
-> demands it later
-> reconstructs it at exactly the bad time
~~~

Potential metadata:

~~~c
typedef struct PipelineBinding {
    ...
    bool prewarmed;
    bool first_use_pending;
    uint64_t ready_us;
    uint64_t first_use_us;
} PipelineBinding;
~~~

A bounded eviction preference could be:

~~~text
in-use
first-use-pending prewarm
recent runtime use
old ordinary entries
~~~

Do not make this unbounded.

Issue #208 owns the measurement and cache-tier experiments.

---

# 8. DXVK: offload shader translation, not only final Vulkan compilation

Reference:

https://github.com/doitsujin/dxvk/releases

Recent DXVK compiler work moved shader compilation/translation fully onto worker threads. Previously, SPIR-V translation still happened on the application thread while Vulkan pipeline compilation was offloaded.

That maps directly to xemu's current boundary.

Today, paths such as the hybrid fragment route still do foreground work conceptually like:

~~~c
code = pgraph_glsl_gen_psh(...);
glsl = mstring_get_str(code);
glsl_size = strlen(glsl);
...
pgraph_vk_hybrid_compiler_submit_async(...);
~~~

Even if glslang is off-thread, source generation, hashing, cache decisions and materialization can still contribute to frame spikes.

## Proposed immutable worker recipe

~~~c
typedef struct PGRAPHVkShaderRecipe {
    uint64_t generation;
    uint64_t ticket;
    ShaderModuleCacheKey key;
    PGRAPHVkGlslCompileConfig config;
    PGRAPHVkCompileUrgency urgency;
    uint64_t first_observed_us;
    uint64_t deadline_hint_us;
} PGRAPHVkShaderRecipe;
~~~

Worker:

~~~text
recipe
 -> generate GLSL
 -> glslang/SPIR-V
 -> return immutable source/SPIR-V metadata
~~~

Renderer thread:

~~~text
validate generation/ticket/key
 -> create/publish Vulkan module
 -> progress retained binding
 -> progress retained pipeline
~~~

No worker may read mutable PGRAPH state after submission.

Issue #207 owns this architecture.

---

# 9. vkd3d-proton: separate translation cache from pipeline behavior

References:

- Shader-cache behavior:
  https://github.com/HansKristian-Work/vkd3d-proton/blob/master/README.md
- Pipeline library / module identifier cache notes:
  https://github.com/HansKristian-Work/vkd3d-proton/blob/master/CHANGELOG.md

vkd3d-proton maintains its own shader conversion cache independently from pipeline construction.

This reinforces xemu's existing direction:

~~~text
portable-ish application data:
    exact normalized guest identity
    generated shader / SPIR-V artifacts

driver/device-local data:
    Vulkan pipeline cache
    module identifiers
    possible pipeline binaries
~~~

Do not collapse every layer into one shader-cache-hit metric.

A warm SPIR-V hit can still require:

~~~text
VkShaderModule
pipeline recipe construction
driver pipeline compilation/link
driver cache lookup/locking
~~~

and can still hitch.

---

# 10. VK_EXT_pipeline_creation_cache_control: make accidental foreground compilation impossible

Reference:

https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_pipeline_creation_cache_control.html

The extension/core Vulkan 1.3 feature exists specifically so an application can prevent unexpected vkCreate*Pipelines compilation.

If the create info includes the fail-on-compile-required flag, the driver must not compile. If it cannot create the pipeline without compilation, it returns VK_PIPELINE_COMPILE_REQUIRED.

This should be treated as a core candidate for Continue and pipeline attribution.

Issue #204 owns it.

### xemu device setup audit

At audited main 134de6616e1d8f5bfe9d919a4e98e0ff7da3c928:

- xemu advertises up to Vulkan 1.3;
- instance.c currently enables a limited optional feature chain (custom border color, shader demote, memory budget extension tracking);
- no xemu state/capability path was found for:
  - pipeline creation cache control;
  - graphics pipeline library;
  - shader module identifier;
  - pipeline binary;
  - the broader dynamic-state family being considered here.

Every experimental lane must first add explicit capability inventory and feature enablement. Do not assume that because a Vulkan header constant exists the device feature is active.

---

# 11. VK_EXT_graphics_pipeline_library: split the monolith

Reference:

https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_graphics_pipeline_library.html

GPL decomposes graphics pipeline compilation into:

~~~text
Vertex Input Interface
Pre-Rasterization Shaders
Fragment Shader
Fragment Output Interface
~~~

This is a strong match for emulation because guest state recombines the same components in many ways.

## Candidate xemu mapping

~~~text
Vertex Input library
    binding descriptions
    attribute descriptions
    relevant topology/interface state

Pre-raster library
    vertex shader
    optional geometry shader
    rasterization state

Fragment library
    specialized fragment shell
    OR ubershader fragment shell

Fragment Output library
    render pass/output formats
    blend state
    depth/multisample/output state where applicable
~~~

Exact assignment must follow Vulkan compatibility rules; this is an investigation map, not an implementation claim.

## Fast link first, optimized link later

Khronos explicitly describes:

~~~text
reusable libraries ready
    -> basic/fast link
    -> usable pipeline
    -> schedule optimized link on another thread
    -> replace when optimized pipeline becomes ready
~~~

That suggests an executable quality ladder:

~~~c
typedef enum PGRAPHVkExecutableQuality {
    PGRAPH_VK_EXEC_NONE,
    PGRAPH_VK_EXEC_UBER_FAST,
    PGRAPH_VK_EXEC_UBER_OPTIMIZED,
    PGRAPH_VK_EXEC_SPECIALIZED_FAST,
    PGRAPH_VK_EXEC_SPECIALIZED_OPTIMIZED,
} PGRAPHVkExecutableQuality;
~~~

Selection:

~~~text
specialized optimized
specialized fast-linked
ubershader optimized
ubershader fast-linked
Continue omission
Wait blocking reference
~~~

This could reduce how often Continue is needed.

Issue #205 owns GPL and stays separate from the first Continue implementation.

---

# 12. GPL example skeleton

Library creation:

~~~c
VkGraphicsPipelineLibraryCreateInfoEXT part_info = {
    .sType =
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
    .flags = part_flags,
};

VkGraphicsPipelineCreateInfo library_ci = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = &part_info,
    .flags =
        VK_PIPELINE_CREATE_LIBRARY_BIT_KHR |
        VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
    ...
};
~~~

Final fast link:

~~~c
VkPipelineLibraryCreateInfoKHR link_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR,
    .libraryCount = library_count,
    .pLibraries = libraries,
};

VkGraphicsPipelineCreateInfo fast_ci = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = &link_info,
    ...
};

/* Deliberately omit LINK_TIME_OPTIMIZATION for first usable executable. */
~~~

Background optimized link:

~~~c
VkGraphicsPipelineCreateInfo optimized_ci = fast_ci;
optimized_ci.flags |=
    VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;

request_pipeline_async(pg, &exact_key, &optimized_ci);
~~~

Measure fast-link GPU performance as well as CPU creation time. Fast to create is not enough if the executable causes a persistent GPU regression.

---

# 13. VK_EXT_shader_module_identifier: speculative warm path without rebuilding SPIR-V

Reference:

https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_shader_module_identifier.html

The extension allows an application to persist a small driver-provided identifier associated with a shader module. On a later run, the identifier may be sufficient to create a pipeline from already cached driver information without recreating the whole module/SPIR-V.

Identifier-only use must pair with fail-on-compile-required semantics.

Potential xemu path:

~~~text
exact ShaderModuleCacheKey
    -> local driver-qualified module identifier
    -> compile-forbidden pipeline creation
       success:
           use pipeline
       compile required:
           normal exact source/SPIR-V async path
~~~

Do not replace xemu's exact SPIR-V store with identifiers. Identifiers are a driver-local optional acceleration.

Issue #208 owns this experiment.

---

# 14. VK_KHR_pipeline_binary: deterministic driver-local binary cache

References:

- Proposal:
  https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_pipeline_binary.html
- Khronos sample:
  https://docs.vulkan.org/samples/latest/samples/extensions/pipeline_binary/README.html

Pipeline binaries allow explicit:

~~~text
pipeline creation parameters
    -> VkPipelineBinaryKeyKHR
    -> application-managed binary data
    -> later pipeline reconstruction
~~~

This provides more deterministic cache control than relying solely on opaque VkPipelineCache behavior.

Possible eventual layers:

~~~text
shareable / guest-semantic tier:
    normalized NV2A identities
    explicit versioned family recipes
    exact SPIR-V artifacts

host-local tier:
    vendor/device/driver identity
    module identifiers
    pipeline binaries
~~~

Do not disable driver internal caching or redesign the existing cache until the experiment proves a benefit and the fallback path is complete.

Issue #208 owns this later lane.

---

# 15. Valve Fossilize: persist the object recipe graph, not merely blobs

Reference:

https://github.com/ValveSoftware/Fossilize/blob/master/README.md

Fossilize records/replays Vulkan create-info graphs for samplers, descriptor-set layouts, pipeline layouts, render passes, shader modules, and pipelines, and represents handle relationships using hashes.

The relevant lesson for xemu is that family history can evolve from:

~~~text
this fallback family appeared
~~~

to:

~~~text
this exact executable recipe was useful, expensive and demanded early
~~~

Conceptual in-memory record:

~~~c
typedef struct PGRAPHVkObservedExecutableRecipe {
    PipelineKey key;
    uint64_t stage_key_hashes[3];

    uint64_t use_count;
    uint64_t cold_miss_count;
    uint64_t too_late_count;
    uint64_t first_demand_frame;
    uint64_t create_cpu_us;
} PGRAPHVkObservedExecutableRecipe;
~~~

Persist only an explicit versioned encoding. Never dump raw PipelineKey or host structs.

This can eventually support:

~~~text
launch
 -> load validated recipe history
 -> rank expensive/early/frequent recipes
 -> prepare stage artifacts
 -> prepare GPL libraries or pipelines
 -> protect first-use-pending hot entries
 -> start/continue guest
~~~

Issue #208 tracks the long-term cache/recipe side; #206 owns identity reduction first.

---

# 16. Updated xemu architecture to evaluate

~~~text
                        NV2A guest state
                              |
                              v
                 canonical / minimal identity
                      (#206, #174)
                              |
              +---------------+----------------+
              |                                |
      exact executable?               early stage recipe
              |                                |
          yes |                                v
              |                    background source/SPIR-V
              |                           (#207)
              |                                |
              |                 reusable module/library pieces
              |                                |
              +----------------+---------------+
                               |
                               v
                    exact full PipelineKey
                               |
                               v
                 compile-forbidden driver probe
                            (#204)
                       /                 \
                  READY             COMPILE_REQUIRED
                    |                      |
                    |              reusable GPL pieces?
                    |                   (#205)
                    |                /           \
                    |          fast link        no
                    |             |              |
                    |             +-------> background build
                    |                         |
                    +-------------------------+
                               |
                         usable executable?
                        /                 \
                     yes                   no
                      |                    |
                  draw normally      miss policy
                                     /        \
                                  Wait      Continue
                                   |           |
                             blocking ref   omit safe draw
                                           + host black
                               |
                    optimized pipeline available later
                               |
                               v
                    atomically prefer better route
                               |
                               v
               history / hot-ready / local cache tiers
                            (#208)
~~~

---

# 17. Recommended implementation order

The old #203 A-E sequence remains useful, but the research lanes change what should happen before large product work.

## Gate 0 — attribution remains mandatory

Before changing defaults, capture whether Batman/Azurik stalls are:

~~~text
true shader/module miss
pipeline compile required
queued-too-late
owner-thread source/materialization
driver pipeline work
resource/surface/texture wait
unrelated CPU/presentation
~~~

## Gate 1 — Child A: typed draw lifecycle

Still first because every later omission policy needs explicit SUBMITTED / OMITTED / FAILED.

## Gate 2 — #204 compile-required probe

Implement/prototype before investing heavily in bespoke Continue logic.

Reason:

~~~text
it tells us exactly when foreground creation would compile
and is the same primitive Cemu uses for async skip-render
~~~

## Gate 3 — generic exact background stage/pipeline construction

Child B/C work, informed by #207:

- stage-generic requests;
- immutable ownership;
- exact dedup;
- demand promotion;
- worker scheduling experiment;
- renderer-thread Vulkan publication.

## Gate 4 — Continue adapts Cemu's proven pattern

At this point Continue becomes relatively small policy work:

~~~text
driver says compile required
 -> queue exact build
 -> use ready fallback if possible
 -> otherwise classify omission safety
 -> omit + black only where user policy permits
~~~

Do not first invent an alternate fake-black shader path.

## Gate 5 — #206 key/minimal recipe work

Can proceed independently once instrumentation is present. Safe key reductions benefit every mode.

## Gate 6 — #205 GPL experiment

Prototype as a separate child PR. If fast links become consistently cheap, update route priority before expanding blackout behavior.

## Gate 7 — #208 warm-cache layers

Only after identities and pipeline construction are stable.

---

# 18. Revised child-PR map

| Child | Primary goal | Prior-art dependency |
| --- | --- | --- |
| A | Explicit draw outcome | Required regardless of strategy |
| B | Generic async shader stages | #207 ownership/scheduling findings |
| C1 | Compile-required foreground probe | #204 / Cemu / Vulkan |
| C2 | Retained demand executable | #207 |
| C3 | Safe omission / Continue | Cemu async skip-render adaptation |
| D | Host-black output + Advanced UI | Only after C3 semantics are stable |
| E | Qualification | Include Hit/Missed/Too-late and worker deadline metrics |
| F (experimental) | GPL fast-link/LTO | #205 |
| G (instrumentation) | Key entropy/minimal recipe | #206 |
| H (later) | Hot-ready/module-id/pipeline-binary cache | #208 |

Do not force F/G/H into the first product repair if C1-C3 solve the measured stall. They remain linked research lanes rather than forgotten ideas.

---

# 19. Measurement schema additions

## Readiness

~~~text
first_demand_count
ready_hit_count
missed_count
too_late_count
unsupported_count
queue_deferred_count

ready_before_demand_ratio =
    ready_hit_count / admissible_first_demands
~~~

## Foreground pipeline probe

~~~text
pipeline_probe_count
pipeline_probe_ready
pipeline_probe_compile_required
pipeline_probe_error
pipeline_probe_cpu_us
~~~

## Work lifecycle

~~~text
job_discovered_us
job_submitted_us
job_started_us
job_finished_us
module_published_us
pipeline_submitted_us
pipeline_finished_us
pipeline_published_us
first_demand_us
first_use_us
~~~

Derived:

~~~text
prediction_lead_us = first_demand_us - job_discovered_us
queue_delay_us = job_started_us - job_submitted_us
worker_us = job_finished_us - job_started_us
publication_delay_us = pipeline_published_us - pipeline_finished_us
readiness_margin_us = first_demand_us - pipeline_published_us
~~~

A negative readiness margin is TOO_LATE.

## Route quality

~~~text
specialized_optimized_draws
specialized_fast_draws
uber_optimized_draws
uber_fast_draws
omitted_draws
blackout_us
blocking_reference_draws
~~~

GPL fields exist only when #205 is enabled.

## Cache/reconstruction

~~~text
prewarmed_first_use_pending
prewarmed_evicted_before_first_use
driver_cache_recreate_us
module_identifier_probe_hit/miss
pipeline_binary_hit/miss
~~~

These remain zero/unavailable unless #208 experiments are active.

---

# 20. Stop conditions

Stop and re-evaluate rather than layering another workaround when:

- Continue still calls a foreground pipeline create that is allowed to compile.
- A skipped draw is later replayed after guest state advances.
- safe omission requires unbounded retained command/vertex/uniform snapshots.
- a cache/key reduction cannot prove semantic equivalence.
- a GPL fast pipeline improves creation latency but causes unacceptable sustained GPU cost.
- background translation reads mutable renderer state after submission.
- demand work is queued but repeatedly classified TOO_LATE; fix deadlines/scheduling before adding more speculative work.
- prewarm creates pipelines that are routinely evicted before first use.
- driver-specific cache data is allowed to bypass vendor/device/driver qualification.
- one experimental extension becomes mandatory for the baseline Vulkan renderer without a maintained fallback.

---

# 21. Source/reference index

## Cemu
- Async shader/pipeline behavior and draw skipping:
  https://wiki.cemu.info/wiki/Release_1.19.0
- Vulkan pipeline compiler, compile-required probe, compiler thread pool:
  https://github.com/cemu-project/Cemu/blob/5e09ec72a43dc8e857f94619d3b0a40c2bd65298/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.cpp
- Earlier shader/pipeline cache distinction:
  https://wiki.cemu.info/wiki/Release_1.16.0

## Dolphin
- Ubershader design:
  https://dolphin-emu.org/blog/2017/07/30/ubershaders/
- Async ubershader vs skip-render route:
  https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexManagerBase.cpp
- Worker resizing and caches:
  https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/ShaderCache.cpp

## RPCS3
- Interpreter precompile:
  https://github.com/RPCS3/rpcs3/issues/17897
- Async with Shader Interpreter:
  https://github.com/rpcs3/rpcs3/issues/18219
- Format-dependent fallback variant miss:
  https://github.com/RPCS3/rpcs3/issues/19085

## Unreal Engine
- PSO precaching:
  https://dev.epicgames.com/documentation/unreal-engine/pso-precaching-for-unreal-engine
- PSO priority levels:
  https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/RHI/EPSOPrecachePriority

## DXVK
- Releases / fully off-thread shader translation:
  https://github.com/doitsujin/dxvk/releases

## vkd3d-proton
- Shader cache:
  https://github.com/HansKristian-Work/vkd3d-proton/blob/master/README.md
- Pipeline library/module identifier notes:
  https://github.com/HansKristian-Work/vkd3d-proton/blob/master/CHANGELOG.md

## Valve Fossilize
- Object graph serialization/replay:
  https://github.com/ValveSoftware/Fossilize/blob/master/README.md

## Vulkan
- Pipeline creation cache control:
  https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_pipeline_creation_cache_control.html
- Graphics Pipeline Library:
  https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_graphics_pipeline_library.html
- Shader module identifier:
  https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_shader_module_identifier.html
- Pipeline binary:
  https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_pipeline_binary.html
- Pipeline binary sample:
  https://docs.vulkan.org/samples/latest/samples/extensions/pipeline_binary/README.html

---

# 22. Handoff rule

Any agent assigned to #203 must read this document before implementing Child B or later.

The implementation question is no longer:

> How do we invent a smooth shader-miss system?

It is:

> Which already-proven techniques fit NV2A/xemu's exact state and ownership model, and what is the smallest measured combination that prevents the reported stalls?

That keeps #203 focused on adapting known solutions, measuring the gaps specific to xemu, and avoiding unnecessary renderer infrastructure.
