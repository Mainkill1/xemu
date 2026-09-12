# research/vulkan-ubershader-design

**Status:** IMPLEMENTATION IN PROGRESS — optional runtime fragment combiner included; default Off; native qualification pending

**Stable baseline:** `9f618d6d8c4c446ef023955f3d4de22f661f61a4` / retained product executable `3489fdcc593e942b92a612bf35a98f509ff0907e3370e1e5f45f2972d83fb16b`

**Implementation base / current main:** `5edff26383c6440da35bc92b9fca35f4a404b03b` / tree `11981a736703553349357cd89926b443901cadb9`

**Current candidate:** `faf00196956d8ec5a8fbaf88bec76b5052aa33bd` / tree `73152f635e02fa2e315ccd5c4a095abbb1a41ae2`; optional runtime fragment-combiner and bounded compiler worker, exact Windows build pending

**Warm-artifact foundation:** [PR #70](https://github.com/Mainkill1/xemu/pull/70), merged as `fc8c5dec9c1aa18883e74b57937d7ec90fdea074`

**Stage 1 contracts and Stage 2 finite combiner oracle:** included directly in this PR as focused commits

**Cause diagnostic:** [PR #68](https://github.com/Mainkill1/xemu/pull/68)

**Exact trace build:** `1ba2c59ee69c6c909bd48b63290fa753ff5fe868` / executable `bf4a3908bcba4d3f6f891ad3562ff1f45a2042feca6c0a244072f1b0a7df4663`

**PR #68 parent:** `f738796284d374f1cf22b05a6f5f643268fc8ab0`

## Dependency status and implementation boundary

**The PR #70 ordering dependency is satisfied.** Its warm shader-management
solution is merged and qualified. Current `main` at `5edff26383c` also includes
PR #76's presentation repair, which does not alter the shader fallback design.

PR #71 is the single feature PR. Its implementation remains divided into
focused commits so the combiner oracle, interpreter semantics, worker
ownership, and production selection can be reviewed independently without
splitting the feature across multiple PRs. The implementation must reuse PR
#70's artifact identity, bounded storage, renderer ownership, failure fallback,
and producer-quiesced persistence lifecycle.

The [external implementation review](https://github.com/Mainkill1/xemu/pull/71#issuecomment-5638411943)
is the detailed implementation appendix for this plan. Its concrete 448-byte
combiner/control ABI, binding-6 layout, ownership, queue, publication, teardown,
and default-off requirements remain inputs to this feature PR.
The packet covers the combiner body and its 18 constants; it does not replace
the current fragment shell or its ordinary reflected uniforms. The current
runtime path uses a dynamic uniform-buffer binding for the packed controls and
keeps vertex, geometry, texture, alpha, depth, clip, and other fragment-shell
state specialized. The background compiler uses deep-owned immutable inputs, is independent of
PR #70's disk-cache toggle, and stops before glslang and renderer teardown.
Qualification must identify the selected GPU UUID, driver/API/features, and PR
#76 presentation transport. The explicit
default-off mask is a defensive startup contract, not a reproduced default-on
failure in current `main`.

## Summary

**Current result:** The prerequisite warm shader-management layer is merged.
This PR includes the fixed 448-byte control ABI, fail-closed admission, hybrid
route metadata, non-creating exact LRU lookup, finite CPU combiner oracle, GLSL
runtime combiner, reflected dynamic control binding, descriptor/control upload,
and an optional renderer route. Enabling the restart-only setting runs admitted
fragment combiners through that runtime route while other shader and pipeline
state remains specialized. Native output and performance qualification have
not been completed.

**Headline:** The interpreter can render an admitted unseen combiner while its
specialized fragment shader compiles in the background. Graphics-pipeline
creation remains synchronous, and the fallback must respect geometry
interfaces, sampler types, vertex input, attachment formats, and host Vulkan
capabilities.

**Next:** Build and execute the exact worker, compiler/reflection, and renderer
tests, then compare the optional runtime combiner with specialized output on a
native Vulkan device, including guest-visible depth, stencil, query, and
render-to-texture effects. The worker publishes SPIR-V and shader modules on the
renderer thread. Graphics-pipeline creation remains synchronous on first
specialized selection and must be measured separately.

Earlier focused CPU, GLSL-generation, compiler/reflection, configuration, and
runtime-key tests passed on the preceding implementation. Their exact-head
rerun plus the new worker tests remain pending. The product renderer calls the
runtime fragment-combiner path only when the default-off setting is enabled. No
native game or performance result is claimed yet.

## Investigation

### Why this work exists

PR #68 measured a repeatable PGR2 Vulkan hitch. At the primary frame, four
first-seen stage sources spent 18.217 ms in glslang inside 43.738 ms of pipeline
preparation. Across the diagnostic run, 121 first-seen stage/source identities
and 20 different-key/same-source repeats consumed 463.419 ms of glslang time.
The 43.738 ms value is aggregate pipeline preparation across 4,808 calls. It is
not a direct measurement of `vkCreateGraphicsPipelines()` or driver compilation.

| Measured work | Observed result | Design implication |
| --- | ---: | --- |
| Primary-frame glslang | 18.217 ms | A cold first-seen path needs more than same-process deduplication |
| Primary-frame measured shader phases | 20.326 ms | A correct ready-to-bind fallback could remove the draw dependency |
| Primary-frame pipeline preparation | 43.738 ms | Shader fallback alone does not guarantee that driver pipeline creation disappears |
| Full-run first-seen stage/source identities | 121 | Game-specific specializations cannot all be inferred before guest execution |
| Full-run different-key/same-source repeats | 20 | Key normalization remains a separate direct optimization |

PR #70 addresses the first practical case: load previously validated SPIR-V
outside gameplay and reuse it on a warm run. This design asks how to handle a
genuinely unseen shader without pausing the draw. It is a subsequent layer on
PR #70's shader management rather than a parallel replacement.

### What can be compiled before seeing a game state

| Artifact | Can xemu prepare it without observing the game? | Limit |
| --- | --- | --- |
| General NV2A interpreter shader | Yes | Must accept guest state as runtime data and can cost more GPU time |
| Finite fallback shader family | Yes | Family boundaries must cover every compile-time interface used by an admitted draw |
| Previously recorded specialized SPIR-V | Yes, on a later run | Requires an exact compatible cache record; this is PR #70's lane |
| Previously recorded graphics pipeline recipes/cache data | Yes, on a later run | Driver data is device/driver compatible rather than universally portable |
| Arbitrary game-specific specialized shader | No | Vertex-program tokens, combiner registers, texture modes, and related state are supplied by guest execution |
| Exhaustive specialized permutation set | Theoretically | The combined NV2A and Vulkan state space is too large to make this the first implementation |

The useful pre-game choices are therefore recorded specialization, prewarming,
or a general interpreter. Scanning an executable or XISO is insufficient: draw
state depends on runtime guest logic, uploaded vertex program tokens, surfaces,
textures, and register writes.

### Current source path

The source audit was reconciled with current `main` at
`5edff26383c6440da35bc92b9fca35f4a404b03b`.

| Area | Current behavior | Consequence for an ubershader |
| --- | --- | --- |
| Binding cache | `shaders.c` keeps 1,024 complete bindings and 51,200 per-stage modules | An ubershader is not a replacement for a missing cache; it is a fallback for a real miss |
| Shader miss | `shader_module_cache_entry_init()` generates GLSL, invokes glslang, creates a device module, and reflects layouts synchronously | Moving this work to a worker helps only if the draw has a ready, correct fallback |
| Vertex stage | `VshState` selects fixed-function generation or embeds the uploaded programmable token stream | A general vertex path must interpret both models or use separate fixed/programmed families |
| Geometry stage | GLSL input/output layout and maximum vertices vary with primitive and polygon mode | One geometry module cannot express every case because execution modes are compile-time |
| Fragment stage | Generated code specializes texture modes, up to eight combiner stages, alpha/depth behavior, window clipping, and interpolation | Runtime control data can replace much specialization, but it increases shader size and branching |
| Texture sampling | GLSL declarations vary among `sampler2D`, `sampler3D`, `samplerCube`, and `usampler2D` | Image dimensionality and sampled numeric type cannot be selected by changing only a uniform |
| Descriptor layout | Current draws use two reflected UBOs and four combined-image-sampler bindings | A fallback needs a stable superset layout and immutable per-draw control storage |
| Pipeline layout | Push-constant size varies with the count of uniform vertex attributes | The fallback layout must have a fixed maximum range or move those values into its control buffer |
| Graphics pipeline | Vertex input, topology, rasterization, depth/stencil, blend, render pass, and shaders are assembled synchronously | A shader-only fallback still stalls if a compatible pipeline must be created on the draw path |
| Optional Vulkan capabilities | Current device setup does not negotiate shader objects, graphics pipeline libraries, or extended dynamic-state families | Those routes require explicit capability discovery and a retained portable path |

Source references:

- [Vulkan shader binding and module caches](https://github.com/Mainkill1/xemu/blob/5edff26383c6440da35bc92b9fca35f4a404b03b/hw/xbox/nv2a/pgraph/vk/shaders.c)
- [Graphics pipeline construction](https://github.com/Mainkill1/xemu/blob/5edff26383c6440da35bc92b9fca35f4a404b03b/hw/xbox/nv2a/pgraph/vk/draw.c)
- [Vertex generator state](https://github.com/Mainkill1/xemu/blob/5edff26383c6440da35bc92b9fca35f4a404b03b/hw/xbox/nv2a/pgraph/glsl/vsh.h)
- [Pixel-combiner and texture state](https://github.com/Mainkill1/xemu/blob/5edff26383c6440da35bc92b9fca35f4a404b03b/hw/xbox/nv2a/pgraph/glsl/psh.c)
- [Geometry execution-mode generation](https://github.com/Mainkill1/xemu/blob/5edff26383c6440da35bc92b9fca35f4a404b03b/hw/xbox/nv2a/pgraph/glsl/geom.c)
- [Vulkan device capability selection](https://github.com/Mainkill1/xemu/blob/5edff26383c6440da35bc92b9fca35f4a404b03b/hw/xbox/nv2a/pgraph/vk/instance.c)

## Options considered

| Option | Cold unseen state | Output behavior | Cost and limitation | Disposition |
| --- | --- | --- | --- | --- |
| Persist and prewarm specialized artifacts | Miss remains on first encounter | Same specialized path | Best direct fix for later runs; cannot create unknown guest state | Proceed in PR #70 |
| Move compilation to a worker and immediately wait | Still blocks | Correct | Moves the work without removing the dependency | Reject |
| Compile asynchronously and skip the draw | Avoids the CPU stall | Missing pixels, depth/stencil writes, query effects, or render-to-texture content | A skipped one-shot effect can remain broken after the compiler finishes | Reject as successful behavior |
| One monolithic shader and pipeline | Intended to avoid the miss | Could be correct in principle | Geometry execution modes, sampler types, vertex input, attachment compatibility, and static state prevent a practical single current-backend pipeline | Do not use as the first architecture |
| Bounded interpreter family plus background specialization | Avoids admitted misses | Draw is rendered immediately; specialized pipeline takes over later | Requires a strict coverage predicate, stable state upload, precreated compatible pipelines, and GPU-cost qualification | Recommended research path |

The Dolphin project uses the same core distinction: skipped asynchronous draws
can produce missing or persistently incorrect output, while hybrid ubershaders
render through a general shader until a specialized shader is ready. That is a
useful architecture precedent, not evidence that Dolphin's shader model can be
copied into NV2A unchanged.

## Recommended architecture

This section describes work that now proceeds in stages from current `main`.
Each implementation branch preserves PR #70's qualified warm-cache behavior
and lifecycle and records its exact base.

### Boundary: a bounded family, not one universal pipeline

Use a finite set of precompiled fallback modules and compatible pipeline
templates. The family key contains only properties that Vulkan requires at
compile or pipeline creation time. Emulated state that can safely vary at draw
time moves into a packed, versioned control block.

| Component | Runtime-interpreted state | Family or pipeline boundary |
| --- | --- | --- |
| Vertex | Uploaded instruction tokens, constants, inline values, fixed-function enables and parameters | Initially split fixed-function and programmable interpreters; interpolation interface and geometry-prefix contract remain static |
| Geometry | Provoking selection, winding data, depth-slope inputs where legal | Input primitive layout, output primitive layout, maximum emitted vertices, and flat/smooth interface require a bounded module family |
| Fragment | Combiner inputs/outputs, stage count, alpha comparison, color key, fog, window clip, depth conversion, and texture-mode operation selection | Flat/smooth interface and typed texture access remain static unless a validated typed-descriptor superset is used |
| Textures | Enabled stage, coordinates, scale, border metadata, shadow compare mode, bump constants | 2D, 3D, cube, and unsigned depth sampling need compatible statically declared image types; unsupported signatures stay specialized |
| Fixed pipeline | Blend constants, stencil reference/masks, viewport, scissor, line width, and every supported dynamically enabled state | Render-pass/attachment compatibility and any state not made dynamic remain in the fallback-pipeline family key |

The first runtime prototype should admit only signatures
for which a ready fallback pipeline was created before gameplay.
Expanding coverage is a later, measured step. A state outside the admitted set
follows the current synchronous specialization path, preserving output at the
cost of the existing stall.

Before runtime selection, start the feasibility oracle with the fragment
combiner. Embed a runtime combiner interpreter in the current generated texture,
clipping, depth, and alpha-test shell and keep ready specialized vertex and
geometry stages. This gives a bounded semantic oracle before attempting a
complete vertex/geometry/fragment fallback. It remains a partially specialized
experiment and cannot remove a cold draw dependency until every shell-changing
field has an admitted precreated family.

### Stable fallback data and descriptor ownership

Define a renderer-owned `FallbackDrawState` with an explicit schema version.
Pack it from the same decoded NV2A semantics used by the specialized generator;
do not maintain an unrelated interpretation of the raw registers.
Do not upload raw C state structures: booleans, enums, padding, and host layout
are not a stable shader ABI. Define explicit-width fields, offsets, bounds, and
endianness, then validate the C and shader layouts against the same schema.

The control record includes:

- fixed-function and programmable vertex mode plus the bounded token stream;
- vertex constants, transform/light data, inline attributes, point parameters,
  and surface scale;
- geometry control values used within the selected static family;
- pixel combiner words, texture-stage program and dependencies, final-combiner
  words, alpha/depth/clip controls, and combiner constants;
- per-stage texture type, scale, border, shadow, convolution, color-key, and
  bump-map data;
- a draw sequence and renderer generation used to reject stale work.

Use a dedicated stable fallback descriptor-set layout. One immutable slice of
a ring buffer owns each recorded draw's control data until its command buffer
retires. A storage buffer is preferable for token and control arrays that need
runtime indexing. Existing vertex and pixel constants can remain in stable
fixed-layout buffers where that avoids copies.

For textures, evaluate two representations with the actual device limits:

| Texture layout | Benefit | Risk |
| --- | --- | --- |
| Typed descriptor superset per guest stage | Reduces fragment module permutations; a uniform branch selects the valid 2D, 3D, cube, or unsigned-depth binding | More descriptors, dummy views, and static shader references; must fit per-stage and per-set limits on supported hosts |
| Fragment family keyed by sampler signature | Uses only the descriptor types a draw needs | More precompiled modules and pipelines; family size must be measured and capped |

Never bind a view to an incompatible shader image type. Fill every statically
reachable unused binding with a type-correct dummy resource. Descriptor sets,
control slices, and sampled images remain alive through GPU completion.

### Miss, worker, and publication flow

The miss decision requires a read-only, non-creating exact-key probe. The
current `lru_try_lookup()` is not that probe: on a miss it may allocate or evict
a node and invoke initialization. A readiness check must not compile, allocate,
evict, change recency, or publish a partial entry.

When that probe finds no ready specialized binding or pipeline:

1. Snapshot the complete specialization key, generator/compiler options,
   effective pipeline key, and renderer generation into an immutable job. The
   job deep-owns all pointed-to arrays and create-info data and does not read
   mutable `PGRAPHState`, live uniforms, cache nodes, or `g_config`.
2. Check the fallback coverage predicate. It must prove that an already-created
   fallback module/pipeline family supports the shader interfaces, samplers,
   vertex input, render targets, and fixed state for this draw.
3. If covered, upload one immutable fallback control record and issue the draw
   through that fallback. Enqueue specialization without waiting. If the
   bounded queue is full, keep using the ready fallback and defer another
   enqueue attempt; queue pressure must not force synchronous compilation for
   an otherwise covered draw.
4. If uncovered, use the current synchronous path. Never call an uncovered
   fallback and never skip the draw.
5. A bounded worker pool generates/loads SPIR-V and creates the specialized
   Vulkan objects. It publishes only a complete successful result carrying the
   full key, renderer generation, and a unique request ticket.
6. The PGRAPH thread drains completed jobs at a draw boundary, checks renderer
   generation, request ticket, and exact key identity, then adopts the result
   through a path that cannot invoke the cache's synchronous miss initializer.
   Publication advances a shader-selection epoch and invalidates the affected
   descriptor, uniform, pipeline-layout, and pipeline binding state so the next
   matching draw actually selects the specialization even when no guest
   register changed.

Do not expose the current mutable `ShaderModuleInfo` to workers. Separate an
immutable compiled artifact from renderer-owned uniform storage and cache
nodes. If workers share a `VkPipelineCache`, follow its synchronization mode;
using one worker-local cache per compiler and merging at a controlled point is
another valid prototype. Shutdown stops intake, joins workers, destroys
unpublished device objects, and then tears down published cache entries.

Recorded fallback commands retain their pipeline, descriptor set, buffers,
images, and samplers until the submission fence retires. A shader module needs
to survive outstanding pipeline creation and any chosen cache-retention policy,
but Vulkan permits destroying it after successful pipeline creation even while
that pipeline is in use. Publishing a specialized result never mutates a
command already recorded with a fallback.

### Pipeline strategy by capability

| Host capability | Proposed use | Required fallback behavior |
| --- | --- | --- |
| Current Vulkan 1.1-compatible path | Precreated bounded monolithic fallback pipelines for proven common families | Uncovered state stays synchronous |
| Pipeline creation cache control | Probe a specialization with `FAIL_ON_PIPELINE_COMPILE_REQUIRED`; enqueue it when the driver reports compilation is required | Treat the result as a scheduling signal, not proof of correctness |
| Extended dynamic state and dynamic vertex input | Reduce fallback pipeline permutations by moving supported raster, depth/stencil, blend, topology, and vertex-input state to commands | Set every required dynamic state before each admitted draw and after command-buffer resets |
| Graphics pipeline library | Prebuild reusable vertex-input, pre-raster, fragment-shader, and fragment-output portions; fast-link only when the device property supports the intended latency | Measure link time and GPU cost; retain monolithic fallback |
| Shader objects plus dynamic rendering | Long-term pipeline-free binding experiment for capable hosts | This is a backend restructuring, not the first prototype; current render-pass and state management must be qualified separately |

Before implementing a capability tier, preserve PR #70's corrected compiler
target policy. Current `main` derives the glslang client and SPIR-V targets from
the renderer's selected `vk_api_version`; an immutable compilation job must
freeze that resolved policy rather than reread mutable renderer or global
configuration. Every fallback tier still needs validation on each admitted API
and feature set.

Vulkan's pipeline cache reduces repeated driver work but does not guarantee a
nonblocking creation. Pipeline-creation cache control can return
`VK_PIPELINE_COMPILE_REQUIRED` instead of stalling. Graphics pipeline libraries
split compile work into reusable portions, while shader objects move shader and
state binding out of monolithic pipelines. All are optional capability paths;
none replaces a correct portable fallback decision.

## Processing flow

### Current / before

The draw builds exact shader state and reuses cached stage modules when
available. A module miss generates GLSL, runs glslang, creates modules, and
reflects layouts synchronously. A graphics-pipeline miss is also resolved
synchronously before the draw is issued.

### Planned background-specialization flow

A ready specialization remains the preferred route. A covered miss would pack
immutable control and job data, issue the draw through a compatible precreated
fallback, and compile the specialization in a bounded worker. Completed work
would be published at a later draw boundary. Uncovered state would retain the
current synchronous specialization path.

### Processing difference

| Area | Current | Candidate design | Expected effect |
| --- | --- | --- | --- |
| First-seen compiler work | Blocks the requesting draw | Runs in a worker after a covered fallback is selected | Remove admitted glslang stalls from the draw dependency |
| First-seen pipeline work | Blocks the requesting draw | Worker creation or reusable pipeline/library path | Reduce admitted driver pipeline stalls |
| Draw output | Specialized shader after waiting | Correct fallback immediately, specialization later | Preserve pixels and side effects without skipped draws |
| GPU work | Specialized program | Interpreter during short miss window | Temporary GPU cost that must be measured |
| CPU work | Synchronous generation and driver calls | State packing, coverage check, queueing, and completion polling | Bounded draw-path overhead |
| Synchronization | Render thread owns all creation | Immutable jobs and controlled publication | No render-thread wait for admitted states |
| Memory | Current caches and reflected per-module storage | Precreated fallback family, control ring, job queue, and in-flight results | Bounded increase with explicit caps |
| Failure | Compiler/pipeline call completes or aborts the path | Fallback continues; failed specialization remains observable and retry-limited | Avoid missing draws and retry storms |

## Planned code scope

The feature stays in this PR. Each stage remains a focused commit with its own
tests and exit condition. The current default-off runtime route interprets the
fragment combiner and sends eligible specialization to one bounded worker. It
does not move Vulkan graphics-pipeline creation off the renderer thread.

| Stage | Intended scope | Exit condition |
| --- | --- | --- |
| 0. PR #70 dependency | Bounded warm artifact reuse and renderer lifecycle | **Complete** — merged as `fc8c5dec9c`; implementation work records current `main` as its base |
| 1. Control and policy contracts | Fixed control ABI, fail-closed admission, non-creating lookup, and bounded route/ticket metadata | **Complete in this PR** — focused strict and sanitizer tests pass |
| 2. Finite combiner CPU oracle | Interpret the admitted packed controls and compare with literal goldens plus an unpacked reference | **Complete in this PR** — zero-to-eight-stage semantic coverage passes without runtime selection |
| 3. Generated GLSL and family-boundary oracle | Execute the interpreter and specialized shaders, then inventory shell-changing texture/clip/depth/alpha and vertex/geometry interfaces | **Partial in this PR** — GLSL generation and real compiler/reflection checks pass; native output and side-effect comparison remains pending |
| 4. Precreated fallback family | Renderer-owned modules, layouts, descriptors, dynamic-state setup, and family coverage predicate behind an off-by-default experiment | **Partial prototype in this PR** — optional fragment route, dynamic controls, descriptors, and isolated cache keys exist; shell-specific modules and pipelines are still created synchronously |
| 5. Background specialization | Immutable jobs, one bounded worker, renderer-thread module publication, cancellation, and shutdown | **Implemented; exact build/test pending** — eligible glslang work leaves the draw path, while first specialized graphics-pipeline creation remains synchronous |
| 6. Coverage and capability expansion | Typed sampler strategy, dynamic-state path, pipeline libraries, or shader objects, each measured separately | Coverage increases without exceeding resource or performance gates |

Current implementation areas are `vk/shaders.c`, `vk/glsl.c`,
`vk/renderer.c`, `vk/renderer.h`, the hybrid worker/policy modules, and the
focused fallback state/interpreter modules. Existing GLSL semantic helpers should be shared or mechanically
cross-checked rather than copied into a second drifting implementation.

## Correctness contract

The fallback is successful only when it performs the draw's observable work.
Avoiding a stall by omitting output is a failure.

| Area | Required oracle |
| --- | --- |
| Fixed-function vertex | Position, colors, fog, texture coordinates, lighting, skinning, texgen, normalization, point size, and z behavior match specialization |
| Programmable vertex | Every supported opcode, masks, swizzles, constants, relative addressing, output mapping, and termination behavior match specialization |
| Geometry | Points, lines, strips, triangles, fans, quads, quad strips, polygon modes, winding, provoking vertex, flat/smooth interpolation, and z-slope behavior match |
| Fragment combiners | One through eight stages, RGB/alpha inputs, mappings, mux/sum, outputs, constants, final combiner, fog, and alpha test match |
| Textures | Disabled, 2D, 3D, cube, unsigned depth, projection, border, shadow compare, alpha kill, color key, bump, dot, reflection, and convolution paths match where currently supported |
| Fixed pipeline | Cull/front face, blend, write masks, depth compare/write, stencil compare/ops/masks/reference, viewport, scissor, topology, and line width match |
| Surfaces and queries | Color, depth, stencil, render-to-texture, readback, clears, and occlusion/report side effects are preserved |
| Precision | NaN, infinity, signed normalization, depth formats, clipping boundaries, and interpolation-sensitive cases use independent expected pixels where available |
| Lifetime | Fallback and specialized objects, descriptors, buffers, textures, jobs, and renderer generations remain valid through GPU and worker completion |
| Failure | Unsupported state, full queue, compile error, device loss, shutdown, and stale job use the defined synchronous/failure path without a skipped draw |

The oracle should replay captured normalized draw states into isolated targets
through both specialized and fallback paths. It must compare pixels and the
depth/stencil/query effects, not only generated source or pipeline creation
counts. Current unimplemented NV2A behavior is not silently broadened by this
work; the first fallback admits only currently supported specialization cases.
Keep the existing specialized generator as an independent reference during the
first oracle stage. Sharing decoded semantics is useful, but sharing the final
evaluator would allow one defect to make both outputs agree. Preserve the
current combiner rule that RGB and alpha results for a stage are computed from
the same pre-stage inputs before either destination write is committed.

## Profiling

### Baseline bottleneck

The measurements below came from PR #68 head
`1ba2c59ee69c6c909bd48b63290fa753ff5fe868`, using xemu executable SHA-256
`bf4a3908bcba4d3f6f891ad3562ff1f45a2042feca6c0a244072f1b0a7df4663`.
That diagnostic head was based on `f738796284d374f1cf22b05a6f5f643268fc8ab0`;
the parent alone is not the measured build identity.

| Measurement | Stable baseline | Exact PR #68 diagnostic | PR #71 candidate |
| --- | ---: | ---: | ---: |
| Primary-frame glslang | Not instrumented in retained baseline | 18.217 ms | Not measured — runtime candidate unqualified |
| Primary-frame pipeline preparation | Not instrumented in retained baseline | 43.738 ms | Not measured — runtime candidate unqualified |
| Full-run glslang | Not instrumented in retained baseline | 463.419 ms | Not measured — runtime candidate unqualified |
| First-seen stage/source identities | Not instrumented in retained baseline | 121 | Not measured — runtime candidate unqualified |

**Profile evidence:** [PR #68 classification report](https://github.com/Mainkill1/xemu-perf-tests/blob/e77436eb3f7084bd8e406e07ede7ec5f318d1e66/docs/evidence/pr68-shader-identity-20260910/classification/REPORT.md)

### Required prototype counters

| Counter | Question answered |
| --- | --- |
| Covered and uncovered specialization misses | How much real draw work can use the fallback? |
| Fallback draws and fallback GPU time | Is temporary interpreter use cheaper than the avoided stall? |
| Worker queue depth, wait, compile, create, and publish time | Does work stay outside the draw dependency and remain bounded? |
| Time from first fallback draw to specialization | How long does the slower GPU path remain active? |
| Specialized/fallback oracle matches | Is coverage correct rather than optimistic? |
| Pipeline compile-required responses and library link feedback | Which driver work actually remains? |
| CPU, GPU, RAM, VRAM, descriptor, pipeline, and cache bytes | Is the resource tradeoff acceptable? |

## Performance results

### Improvement convention

Every percentage is **Improvement %**: positive is good and negative is bad.
The `Raw +` cell states whether a larger raw value is good or bad.

| Raw + | Raw metric direction | Improvement % |
| --- | --- | --- |
| `+good` | Higher is better, such as FPS or completed work | `100 × (candidate / reference - 1)` |
| `+bad` | Lower is better, such as time, stalls, or memory | `100 × (reference - candidate) / reference` |
| `N/A` | Context only or no valid runtime comparison | `N/A` with a reason |

### Headline results

| Workload | Renderer | Metric | Raw + | Stable | Previous main | Candidate | Improvement vs stable | Improvement vs previous main |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| PGR2 snapshot cold | Vulkan | Maximum interval | `+bad` | Reuse retained result | Collect with implementation | N/A | N/A | N/A |
| PGR2 snapshot cold | Vulkan | Primary-frame shader/pipeline wait | `+bad` | Not instrumented | Not measured on exact previous main | N/A | N/A | N/A |
| PGR2 snapshot warm | Vulkan | Average, p95, p99, maximum, stalls | `+bad` | Reuse retained result | Collect with implementation | N/A | N/A | N/A |
| PGR2 full start | Vulkan/OpenGL | Average, p95, p99, maximum, stalls | `+bad` | Reuse retained result | Collect with implementation | N/A | N/A | N/A |
| Morrowind snapshot | Vulkan/OpenGL | Average, p95, p99, maximum, stalls | `+bad` | Reuse retained result | Collect with implementation | N/A | N/A | N/A |

No Morrowind full-start route exists. Morrowind qualification uses its
maintained snapshot path because reaching its slow points requires deliberate
in-game navigation.

## XISO results

| Gate | OpenGL | Vulkan |
| --- | --- | --- |
| Targeted fallback shader corpus | Not needed — Vulkan-only feature | Not run — runtime candidate unqualified |
| Full catalog and output oracle | Not run — runtime candidate unqualified | Not run — runtime candidate unqualified |
| Validation errors | Not needed — Vulkan-only feature | Not run — runtime candidate unqualified |

## Resource results

| Metric | Raw + | Stable | Previous main | Candidate | Improvement vs stable | Improvement vs previous main |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Fallback module/pipeline RAM | `+bad` | Reuse retained result | Collect with implementation | N/A | N/A | N/A |
| GPU/driver memory | `+bad` | Reuse retained result | Collect with implementation | N/A | N/A | N/A |
| Control-ring and descriptor bytes | `+bad` | 0 | 0 | N/A | N/A | N/A |
| Worker queue and artifact bytes | `+bad` | 0 | PR #70 measured separately | N/A | N/A | N/A |
| Startup fallback preparation | `+bad` | 0 | 0 | N/A | N/A | N/A |
| Host GPU time per fixed guest work | `+bad` | Reuse retained result | Collect with implementation | N/A | N/A | N/A |

Caps must be fixed before runtime testing: worker count, queued jobs, control
ring bytes, fallback descriptors, module/pipeline count, artifact memory, and
startup preparation time. Saturation must be observable and must fall back to
the ready fallback when the draw is covered. Only an uncovered draw, or one
without a ready compatible fallback, uses the synchronous correct path.

## Validation status

| Test | OpenGL | Vulkan |
| --- | --- | --- |
| PR #70 qualification and merge | Not needed | PASS — merged and qualified |
| Config default, persistence, and restart publication | Not needed | PASS — focused configuration test |
| Control packer and finite CPU combiner oracle | Not needed | PASS — focused unit and sanitizer tests |
| Generated GLSL and real compiler/reflection ABI | Not needed | PASS — focused host tests |
| Runtime route, key isolation, and control upload helpers | Not needed | PASS — focused host tests |
| Specialized/runtime-combiner GPU differential corpus | Not needed | Not run — native oracle pending |
| Cold, warm, and same-process specialization | Not needed | Not run — PR #71 candidate pending |
| Unsupported capability/state fallback | Not needed | Not run — native candidate pending |
| Worker saturation, failure, cancellation, and shutdown | Not needed | Source/checkpatch PASS; exact compiled unit run pending |
| Representative/partial XISO | Not run — Vulkan-only feature | Not run — runtime candidate unqualified |
| Profiling comparison | Not needed | Not run — runtime candidate unqualified |
| Resource comparison | Not run — Vulkan-only feature | Not run — runtime candidate unqualified |
| Morrowind snapshot | Not run — Vulkan-only feature | Not run — runtime candidate unqualified |
| PGR2 full start | Not run — Vulkan-only feature | Not run — runtime candidate unqualified |
| PGR2 snapshot | Not run — Vulkan-only feature | Not run — runtime candidate unqualified |
| Full XISO | Not run — runtime candidate unqualified | Not run — runtime candidate unqualified |
| Final visual validation | Not run — runtime candidate unqualified | Not run — runtime candidate unqualified |

The eventual behavioral candidate must compare its exact binary with both the
immediately previous `main` and fixed baseline. Reuse the existing baseline
binary and results. A valid unexplained regression above 2%, including average,
p95, p99, maximum, stalls, CPU, GPU, RAM, or VRAM, keeps the candidate on hold.

## Risks and controlled failure

| Risk | Required control |
| --- | --- |
| Interpreter semantic drift | Share decoded state/IR and require differential output plus side-effect oracles |
| Large shader compiles slowly at startup | Precompile bounded artifacts at build time where portable or at renderer initialization with a startup budget; report cost |
| GPU throughput or tail regression | Use fallback only while specialization is pending; measure fixed guest work and fallback residence time |
| Pipeline permutation explosion | Separate runtime state from compile-time family identity; cap family and refuse uncovered states |
| Optional extensions fragment support | Capability tiers never alter correctness; unsupported hosts retain current path |
| Mutable state races | Immutable jobs and control slices, renderer generation checks, render-thread publication |
| Published result is not selected | Advance a selection epoch and invalidate descriptor/uniform/layout/pipeline binding state on adoption |
| Vulkan lifetime violation | Retain draw-referenced objects until GPU completion; retain modules through pipeline creation; join workers before device teardown |
| Compiler target exceeds admitted device | Negotiate target environment from the supported device contract and validate emitted SPIR-V on every capability tier |
| Repeated failed compilation | Observable error state with bounded retry/backoff; fallback remains available only when correct |
| Descriptor limit or type mismatch | Query limits, use type-correct bindings/dummies, and reject an unsupported signature |
| Cold launch improves while warm launch worsens | Report cold, warm, and same-process results separately from PR #70 |

## Decision

**Result:** OPTIONAL HYBRID RUNTIME IMPLEMENTED; exact build, native behavior, and performance remain unqualified.

The recommended architecture is a bounded hybrid interpreter family with an
explicit coverage predicate and background specialization. It directly targets
the cold first-seen case that PR #70 cannot know in advance, while PR #70
remains the qualified warm-run repair and lifecycle foundation. A worker that
is immediately waited on provides no benefit, and draw skipping is not an
acceptable correctness result.

This PR contains the explicit state ABI, validated packer, selection/ticket
metadata policy tests, non-creating exact cache probe, CPU oracle, generated
GLSL interpreter, dynamic control upload, and optional renderer route. It does
not define a placeholder digest as the full key and does not treat metadata
validation as proof that a Vulkan executable bundle is ready. Bounded background specialization and renderer-thread module publication are
in this draft. Graphics-pipeline creation is still synchronous and remains a
measured limitation. Capability expansions such as dynamic state, pipeline libraries, and
shader objects remain separately measured stages.

## Evidence and primary references

- [PR #68 shader-miss classification](https://github.com/Mainkill1/xemu/pull/68)
- [PR #70 validated SPIR-V reuse lane](https://github.com/Mainkill1/xemu/pull/70)
- [Khronos pipeline management sample](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html)
- [Vulkan pipeline creation cache control](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_pipeline_creation_cache_control.html)
- [Vulkan graphics pipeline library proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_graphics_pipeline_library.html)
- [Vulkan shader object sample](https://docs.vulkan.org/samples/latest/samples/extensions/shader_object/README.html)
- [Dolphin hybrid ubershader design](https://dolphin-emu.org/blog/2017/07/30/ubershaders/)
- [Repository workflow](../repository-workflow.md)
- [Performance PR template](../../evidence/wiki-xiso-per-test/PERFORMANCE_PR_TEMPLATE.md)

## Final summary

Specific game shaders cannot generally be specialized before the guest exposes
their vertex tokens, combiner state, texture signatures, and pipeline state.
A precompiled general interpreter can cover unseen work, but xemu needs a
bounded family because several Vulkan interfaces remain compile-time or
pipeline-time decisions. The current optional path interprets admitted fragment
combiner state at runtime while retaining the specialized fragment shell,
vertex and geometry stages. It compiles eligible specialized fragment SPIR-V on
one worker and publishes shader modules on the renderer thread; graphics
pipeline creation and a new fallback shell can still be synchronous. It claims
no improvement. The PR #70 ordering dependency is complete. PR #71 now includes
the fallback state ABI, packer, policy, non-creating cache query, finite combiner
semantics, generated GLSL, and opt-in renderer route in one feature PR. Native specialized-versus-runtime output, worker execution, and performance
qualification remain the next gates. First fallback and specialized
graphics-pipeline creation remain synchronous and must be reported separately.
