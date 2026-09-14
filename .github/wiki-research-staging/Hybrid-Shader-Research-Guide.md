# Hybrid Shader Research Guide

This page is a project-independent guide for adding a fallback shader or ubershader to an emulator, translation layer, or state-driven renderer without turning a rare cold miss into steady-state overhead.

The xemu case study is [[Vulkan Ubershader & Hybrid Specialization|Vulkan-Ubershader-and-Hybrid-Specialization]]. The principles here are intended to transfer to other guest GPUs and host APIs.

## When this architecture is appropriate

A hybrid path is worth investigating when profiling establishes all of the following:

1. A meaningful frame tail contains first-use shader or graphics-pipeline work.
2. The guest operation can be executed by a finite generic evaluator with equivalent visible behavior.
3. A compatible fallback can already be executable before the exact specialization is ready.
4. The temporary GPU cost of the fallback is acceptable.
5. The renderer can keep compilation, publication and lifetime ownership bounded.

Do not start from “other emulators have an ubershader.” Start from a measured draw dependency.

## First prove the dependency

Time each layer independently:

```text
guest state extraction
source generation
GLSL/HLSL/WGSL compilation
intermediate artifact validation
host shader-module creation
reflection/layout extraction
pipeline-layout creation
render-pass/attachment compatibility
host graphics-pipeline creation
cache lookup/publication
command recording
```

A pipeline-preparation region is not proof that the driver spent that time compiling a pipeline. A binding miss is not proof of a compiler call. An intermediate artifact hit is not proof that an executable pipeline exists.

Retain exact identities in the trace:

- guest shader/program identity;
- generated-source identity;
- fallback-family identity;
- exact pipeline identity;
- worker generation and ticket;
- frame and draw identifiers.

## Use an artifact ladder

Define the readiness levels before writing route logic:

```text
guest program
    ↓
generated source
    ↓
compiled intermediate artifact
    ↓
host shader module
    ↓
shader binding / interface metadata
    ↓
pipeline layout and attachment contract
    ↓
complete host graphics pipeline
    ↓
executable route
```

Never collapse these into one Boolean called `shader_ready`.

A recommended candidate type is:

```c
typedef struct ExecutionCandidate {
    RouteKind route;

    ShaderBinding *binding;
    GraphicsPipeline *pipeline;

    PipelineKey pipeline_key;
    uint64_t pipeline_hash;

    ResourceState resources;
} ExecutionCandidate;

static inline bool candidate_is_executable(
    const ExecutionCandidate *candidate)
{
    return candidate->binding &&
           candidate->pipeline &&
           candidate->resources != RESOURCE_UNAVAILABLE;
}
```

## Keep three identities separate

### Desired specialization

The exact guest state and host fixed-function state whose optimized pipeline should eventually run.

### Fallback family

The compile-time state shared by a generic evaluator. Only fields whose behavior genuinely moved into runtime data may be removed from this key.

### Exact executable pipeline

The complete host pipeline recipe, including stage interfaces, vertex input, attachment formats, blend/depth/raster state, layout and compatible pass state.

A generic fragment evaluator does not make vertex programs, render-target formats, vertex layouts or fixed pipeline state generic automatically.

## Build the semantic oracle first

Before threading and caching, prove the fallback evaluator against the established implementation.

A useful order is:

```text
1. Extract shared guest semantics.
2. Implement a CPU oracle or reference evaluator.
3. Feed identical recorded states to specialized and generic paths.
4. Compare outputs and side effects.
5. Admit only states that pass.
6. Keep unsupported states on the established route.
```

Test edge cases deliberately:

- register aliasing;
- delayed versus immediate writes;
- signed and unsigned mappings;
- NaN, infinity and negative input behavior;
- clamp and rounding order;
- mux control boundaries;
- disabled stages and final stages;
- depth, discard and alpha-test side effects;
- constants and texture/fog inputs.

A fallback is a second implementation of guest semantics. It needs the same level of correctness evidence as a new interpreter.

## Fail closed

The admission function should have one meaning:

```text
true  = this exact guest program is proven representable
false = use the established specialized path
```

Do not broaden coverage by silently ignoring fields or substituting “safe” arithmetic. Record a rejection reason so unsupported families can be prioritized from real workload data.

```c
typedef enum AdmissionRejectReason {
    ADMIT_OK,
    ADMIT_UNSUPPORTED_OPCODE,
    ADMIT_UNSUPPORTED_MAPPING,
    ADMIT_UNSUPPORTED_SIDE_EFFECT,
    ADMIT_INVALID_ENCODING,
} AdmissionRejectReason;
```

## Design the hot path before the miss path

The common successful route must return before another route is prepared.

```text
current executable still valid?
    → return

complete specialized executable ready?
    → return

complete fallback executable ready?
    → prepare only required runtime data
    → schedule specialization once if due
    → return

otherwise
    → uncovered path
```

Do not perform the following on every ordinary draw:

- build both route keys;
- validate the fallback program;
- scan pending-job arrays;
- lock empty result queues;
- rebuild complete pipeline recipes;
- create pipeline layouts;
- compare large control packets;
- recalculate backoff policy.

Use state generations or epochs for the current executable:

```c
typedef struct ActiveExecution {
    ShaderBinding *binding;
    GraphicsPipeline *pipeline;
    RouteKind route;

    uint64_t shader_epoch;
    uint64_t fixed_state_epoch;
    uint64_t vertex_layout_epoch;
    uint64_t attachment_epoch;
} ActiveExecution;
```

The fast check should be scalar comparisons whenever possible.

## Specialized-first short circuit

When route reselection is required:

```c
ShaderBinding *specialized = find_specialized_binding_ready(state);
if (specialized) {
    GraphicsPipeline *pipeline =
        find_specialized_pipeline_ready(state, specialized);
    if (pipeline) {
        activate_specialized(specialized, pipeline);
        return;
    }
}

/* Only now prepare or probe fallback state. */
```

This avoids paying fallback admission, packet construction and resource checks for states whose optimized route is already executable.

## Give fallback execution its own reuse path

A fallback can remain active for many draws while specialization is pending. That period needs a fast path just as much as specialized execution.

```c
if (active.route == ROUTE_FALLBACK &&
    desired_state_unchanged() &&
    active_pipeline_state_unchanged() &&
    !promotion_ready_for_active_key()) {

    update_fallback_runtime_data_if_dirty();
    retry_promotion_only_if_due();
    return;
}
```

The renderer should not rediscover pending shader and pipeline jobs on every draw.

## Represent promotion as persistent state

A recommended state machine is:

```text
ABSENT
SHADER_PENDING
SHADER_READY
PIPELINE_PENDING
READY
QUEUE_BACKOFF
FAILED_BACKOFF
FAILED_PERMANENT
```

Store it beside the exact desired specialization identity.

```c
typedef struct PromotionWork {
    ExactPipelineKey key;
    uint64_t key_hash;

    uint64_t generation;
    uint64_t ticket;

    PromotionStatus status;
    uint64_t retry_after_frame;
    unsigned attempts;
    unsigned max_attempts;
} PromotionWork;
```

Queue-full and resource pressure should not consume a compile attempt. Persistent driver failures should not be submitted indefinitely.

Use frame or monotonic-time backoff, not draw-count backoff.

## Separate required work from speculative work

A worker marked “high priority” cannot interrupt a compile already executing. If a draw-required job shares one non-preemptible worker with speculation, it can still wait behind optional work.

Options include:

- independent required and speculative workers;
- a reserved urgent execution lane;
- direct required compilation on the requesting thread while speculation remains isolated;
- cancellation/preemption only if the compiler API genuinely supports it.

The required path must be measured for queue wait, compile time and caller wait separately.

## Deep-own every cross-thread input

Host graphics APIs commonly use pointer-rich create structures. A shallow copy is not an immutable job.

For a Vulkan graphics-pipeline request, consider:

- stage arrays and entry-point names;
- specialization constants;
- vertex bindings and attributes;
- viewport/scissor arrays;
- sample masks;
- blend attachments;
- dynamic states;
- `pNext` chains;
- pipeline layout;
- render pass;
- shader-module lifetime;
- pipeline cache and device lifetime.

Either deep-copy every supported structure or construct a typed, owned recipe directly inside the job.

A useful test submits a recipe, overwrites/frees every source-side temporary, then confirms that the worker still observes the original values.

## Prefer typed internal recipes

If the production renderer creates a fixed bounded recipe itself, avoid reparsing it as if it came from an untrusted plugin.

Use a typed job:

```c
typedef struct OwnedPipelineRecipe {
    HostGraphicsPipelineInfo info;
    HostShaderStage stages[MAX_STAGES];
    HostVertexBinding bindings[MAX_BINDINGS];
    HostVertexAttribute attributes[MAX_ATTRIBUTES];
    HostBlendAttachment attachments[MAX_ATTACHMENTS];
    HostDynamicState dynamic_states[MAX_DYNAMIC_STATES];
} OwnedPipelineRecipe;
```

Validate external/guest semantics once. Use assertions for renderer-internal invariants. Repeated runtime checks of known `sType`, fixed array capacity and hard-coded entry-point names add branches and walks without improving guest correctness.

## Cache small finite layout families

Pipeline layouts are often determined by a small contract:

- descriptor-set layouts;
- push-constant stage mask;
- push-constant offset and size.

If only a finite number of contracts exist, cache or precreate them. Do not call the host layout-creation function for every specialized pipeline if the layout is identical.

Likewise, reuse a compatible render pass or rendering signature from the active fallback when the specialization has the same attachments.

## Treat resource pressure separately from incompatibility

Use at least three resource states:

```text
READY
NEEDS_ROLLOVER
UNAVAILABLE
```

Examples of `NEEDS_ROLLOVER`:

- descriptor-set pool/index exhausted but reset can recover;
- uniform/control staging ring requires submission/reset;
- current command batch must retire before a safe cache eviction.

Examples of `UNAVAILABLE`:

- required descriptor binding absent;
- fallback program unsupported;
- stage interface mismatch;
- attachment or pipeline family incompatible.

A temporary rollover should remain on the same correct route. It should not force an unrelated synchronous specialization.

## Reserve publication capacity

A bounded worker queue is not enough. Completed objects also require bounded storage and a place in the renderer cache.

Before accepting expensive background work, guarantee one of:

- an actual reserved cache node;
- a counted future publication slot;
- a bounded retained-result queue that can wait for safe eviction.

Otherwise many jobs can all observe one free slot and later discard successful results.

## Use side-effect-free probes

A readiness probe must not:

- allocate;
- evict;
- compile;
- create a host object;
- change LRU recency;
- submit or finish a command buffer.

Name probe and creating functions differently:

```c
find_ready_pipeline(...);        /* no side effects */
get_or_create_pipeline(...);     /* explicit creation */
reserve_pipeline_slot(...);      /* explicit ownership */
```

Test a missing probe for zero init callbacks, zero eviction callbacks, unchanged counts and unchanged recency.

## Publish complete artifacts

A background completion should contain or identify everything needed for the promised readiness level.

For exact pipeline promotion:

```text
compile source
    ↓
materialize modules and immutable metadata
    ↓
construct exact graphics pipeline
    ↓
validate generation + ticket + exact key
    ↓
install complete cache entry
    ↓
mark route selectable
```

Do not mark the route ready after SPIR-V or module completion if a later draw can still block on pipeline creation.

## Do not globally invalidate unrelated routes

A completed pipeline for state B should not force state A to rebuild or re-extract guest state.

Prefer exact-key readiness notification:

```c
if (completion.key_hash == desired_specialized_key_hash) {
    active_selection_dirty = true;
}
```

Unrelated completions can enter the cache without disturbing the current executable.

## Bound publication per frame

A limit of “two completions per function call” is not a frame budget if the function is called once per draw.

Use one shared frame budget:

```c
typedef struct PublicationBudget {
    uint64_t frame_id;
    unsigned shader_results;
    unsigned pipeline_results;
    uint64_t cpu_us;
} PublicationBudget;
```

Bound both count and CPU time. An expensive single host call cannot be preempted, so move expensive construction off the renderer before relying on a publication time budget.

## Split static controls from dynamic values

Generic evaluators often receive a packet containing both program identity and per-draw values.

Cache validated static program data:

```text
opcodes / mappings / destinations / control flags
```

Update dynamic data independently:

```text
constants / textures / fog / current registers
```

Use source epochs rather than comparing a large packet every draw.

## Trace the decision and the cost

For every slow frame, record:

- route selected;
- reason;
- complete and partial readiness of each candidate;
- uncovered route chosen;
- compile queue wait and compile duration;
- module materialization time;
- pipeline-layout time;
- render-pass/signature lookup time;
- graphics-pipeline creation time and thread;
- publication/adoption result;
- resource rollover and fence wait;
- worker overlap with vCPU/render threads;
- guest work completed.

Classify findings clearly:

```text
CONFIRMED SOURCE BEHAVIOR
FOCUSED TEST RESULT
OBSERVED IN TRACE
PERFORMANCE HYPOTHESIS
QUALIFIED RESULT
HISTORICAL RESULT
```

## Correctness tests

Minimum focused coverage:

### Semantic evaluator

- supported opcode/mapping matrix;
- register aliasing and delayed writes;
- constants and texture inputs;
- final output and side effects;
- rejected states remain rejected;
- specialized and fallback outputs agree.

### Ready probes

- exact collision handling;
- no allocation or eviction on miss;
- no recency change;
- complete binding and pipeline required.

### Worker ownership

- deep-owned strings and arrays;
- stale generation rejected;
- ticket mismatch rejected;
- shutdown waits for active work;
- result destruction after worker teardown;
- failure returns explicit status.

### Routing

```text
specialized complete
    → specialized

specialized module only, fallback complete
    → fallback

specialized binding only, fallback complete
    → fallback

fallback complete, queue full
    → fallback without synchronous compile

fallback needs resource rollover
    → fallback after rollover

neither route complete
    → explicit uncovered policy
```

### Operation-order call counts

For a complete specialized hit:

```text
fallback probes             0
fallback packet validations 0
queue submissions           0
worker locks                0
```

For a stable fallback with pending specialization:

```text
new shader submissions      0
new pipeline submissions    0
full route reprobes         0
program validations         0
```

## Performance experiment design

Compare:

```text
previous main
candidate feature Off
candidate feature On
```

Use matched workload seeds and report cold/warm state explicitly.

At minimum retain:

- frame interval distribution through p99.9 and maximum;
- explicit stall counts;
- guest work totals;
- fallback coverage;
- uncovered reasons;
- foreground versus background compile/create counts;
- worker CPU overlap;
- GPU fallback cost;
- cache/publication pressure;
- correctness hashes and validation errors.

A good average does not compensate for new rare stalls unless the project’s acceptance criteria explicitly allow them.

## Common failed designs

### “Module ready means shader ready”

Failure: the next draw blocks in graphics-pipeline creation.

Correction: promote only complete executable pipelines.

### “Queue priority solves required work”

Failure: the active speculative job is not preempted.

Correction: isolate required execution capacity.

### “Fallback compatible means fallback can be created now”

Failure: synchronous fallback construction becomes another cold stall.

Correction: distinguish compatible from already executable.

### “Queue full means compile synchronously”

Failure: a covered draw loses its latency protection during pressure.

Correction: keep using fallback and retry later.

### “Resource rollover means route unavailable”

Failure: a temporary descriptor/staging reset redirects to specialization.

Correction: remain on the correct route and recover resources.

### “Two publications per draw is bounded”

Failure: a draw-heavy frame publishes many results.

Correction: budget per frame.

### “One free cache slot is enough for many pending jobs”

Failure: successful results are discarded.

Correction: reserve publication capacity.

### “Validate everything every draw”

Failure: miss-path safety checks become steady-state overhead.

Correction: validate at admission/ownership boundaries and use fast scalar epochs thereafter.

## Review checklist

```text
[ ] The measured stall is separated into source, compiler, module,
    layout and pipeline phases.

[ ] Source, SPIR-V, module, binding and pipeline readiness are distinct.

[ ] Desired specialization, fallback family and exact pipeline have
    separate identities.

[ ] A correctness oracle exists before asynchronous integration.

[ ] Unsupported guest states fail closed.

[ ] Current executable reuse is the first branch.

[ ] Specialized readiness is checked before fallback preparation.

[ ] Stable fallback draws have a dedicated reuse fast path.

[ ] Required work cannot wait behind active speculative work.

[ ] Every worker input is deeply owned or explicitly retained.

[ ] Renderer cache mutation remains under one ownership model.

[ ] Complete pipelines, not modules, trigger promotion.

[ ] Queue and failure states persist with bounded retries.

[ ] Retry timing uses frames/time, not draw count.

[ ] Temporary resource rollover remains on the same route.

[ ] Pipeline publication capacity is reserved or bounded.

[ ] Empty result queues do not require a hot-path mutex.

[ ] Static program validation is not repeated every draw.

[ ] Completion adoption is budgeted per frame.

[ ] Unrelated completion does not globally invalidate current state.

[ ] Feature Off preserves the baseline layout and hot path.

[ ] Performance reports include guest work and correctness evidence.
```

## xemu case study

The xemu implementation can be followed through:

- [[Vulkan Shader Binding & Compilation|Vulkan-Shader-Binding-and-Compilation]]
- [[Vulkan Ubershader & Hybrid Specialization|Vulkan-Ubershader-and-Hybrid-Specialization]]
- [[Vulkan Performance|Vulkan-Performance]]
- [PR #68](https://github.com/Mainkill1/xemu/pull/68)
- [PR #70](https://github.com/Mainkill1/xemu/pull/70)
- [PR #71](https://github.com/Mainkill1/xemu/pull/71)

The current branch is research, not a universal reference implementation. Reuse the measurement discipline, state separation, operation ordering and ownership model; adapt guest semantics and host API details to the target project.
