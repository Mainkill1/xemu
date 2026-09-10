# Vulkan shader-binding hot path

## Measured problem

A repeatable PGR2 snapshot frame creates eight graphics pipelines. Exact
diagnostic runs attribute 23.906–25.034 ms of that frame to
`pgraph_vk_bind_shaders()` and 12.208–13.538 ms to
`vkCreateGraphicsPipelines()`. Pipeline-key construction, hashing, cache lookup,
and pipeline-layout creation total less than 1.1 ms.

The shader-binding event exists on the current `main` tree and predates the
unrelated cubemap repair in PR #14. This branch owns only the shader-binding
cost. Pipeline creation remains a separately measured follow-up unless the same
cache-lifetime repair necessarily affects both stages.

Eight graphics-pipeline misses do not establish eight shader compilations.
`pgraph_vk_bind_shaders()` also performs state checks and uniform updates, and
the existing renderer already keeps a 1,024-entry shader-binding cache plus a
51,200-entry per-stage module cache. Hash construction is not the primary
target: key construction, hashing, lookup, and layout creation together account
for less than 1.1 ms in the measured frame.

## Independent source review

An independent review of the initial planning head
`177b3ecbbb4a800ea1afca1def033e59593cc0b3` identified three different sources
of avoidable work. They must be measured and changed independently so that a
gain in one path cannot hide a regression in another.

| Candidate cause | Source-level observation | Disposition in this PR |
| --- | --- | --- |
| Redundant shader-module variants | `alpha_func` remains in the fragment state key while alpha testing is disabled, even though no alpha-test GLSL is emitted. `point_params` values also appear in vertex state while the generated fixed-function shader reads them as uniforms. | Instrument full state-key identity against exact generated-GLSL identity. Canonicalize a field only after the trace and focused oracle prove identical code and output. |
| Redundant graphics-pipeline variants | Raw `CONTROL_0_ALPHAREF`, `ZOFFSETBIAS`, and `ZOFFSETFACTOR` values are represented in `PipelineKey`, while the renderer supplies them as fragment uniforms. | Separate candidate and PR. It targets the measured 12.208–13.538 ms driver pipeline-creation region and must not be hidden inside shader-artifact persistence. |
| Cross-run shader compilation | The stage-module cache is process-local. A process restart repeats GLSL generation and glslang compilation even for an artifact compiled in a prior run. | A bounded persistent SPIR-V experiment may be included, but it must report cold launch, warm-cache launch, and same-process revisit separately. It is not evidence that a genuinely new shader can be avoided. |

The review also identified two experiments that remain separate: measure
glslang SPIR-V validation enabled versus disabled on the captured corpus, and
evaluate Vulkan driver pipeline-cache persistence. Disabling validation changes
compiler policy; driver pipeline data targets a different 12–14 ms region.
Neither belongs in the first shader-identity change.

Each module-cache miss must record the stage, complete state-key identity,
exact generated-source identity, first-seen/eviction/recompile reason, and
separate durations for GLSL generation, compilation, reflection, Vulkan module
creation, and uniform updates. `NV2A_PROF_SHADER_GEN` counts binding creation
that can reuse cached modules and therefore is not an exact compiler-invocation
counter.

## Change boundary

The candidate will first determine whether the measured misses are genuinely
new source. A persistent-artifact component may then reduce repeat-run shader
work by reusing validated SPIR-V across equivalent module identities. It must
preserve:

- complete vertex, geometry, and fragment key identity;
- Vulkan device ownership of every `VkShaderModule`;
- reference counts and eviction cleanup;
- uniform reflection data and layout identity;
- unsupported-driver and cache-miss fallback behavior;
- deterministic output with an empty or invalid persistent cache.

The first implementation step splits shader-module lookup, GLSL generation,
SPIR-V compilation/reflection, and Vulkan module creation time and compares the
full state key with generated-source identity. Only a stage that owns measured
cost receives a behavioral change.

For persistent artifacts, the cache key includes shader stage, the complete
GLSL source, debug/compiler configuration, generator/build identity, and target
Vulkan/SPIR-V versions. A hash selects a candidate entry; the full identity is
compared before reuse. Files have bounded entry and total sizes, use atomic
replacement, and fall back to compilation when missing, stale, truncated,
corrupt, oversized, or unwritable. Vulkan handles and mutable uniform storage
are never serialized. Device-owned modules and reflection state are recreated
for each renderer instance.

The reported apparent uniform-layout allocation leak is a lifecycle concern.
It must be verified and tracked independently; it is not evidence for the cold
shader burst and is outside this performance patch.

## Acceptance gate

The exact candidate must pass every row below against both the immediately
previous `main` and the fixed cycle baseline. A single valid unexplained
regression remains a failure.

| Gate | Required coverage |
| --- | --- |
| Focused unit/fault tests | Hit, miss, collision, eviction, invalid cache, fallback, cleanup |
| Full XISO | Complete catalog and output oracle on Vulkan and OpenGL |
| PGR2 full start | Vulkan and OpenGL |
| PGR2 snapshot | Vulkan and OpenGL, including the eight-pipeline burst |
| Morrowind full start | Vulkan and OpenGL with verified gameplay progression |
| Morrowind snapshot | Vulkan and OpenGL with verified Start/B, image transition, and guest progress |
| Performance | Average, p95, p99, maximum, and stalls |
| Resources | CPU, GPU, memory/cache growth, lifecycle, and process/private-disk cleanup |

Positive Improvement percentages are favorable. Timing and resource metrics
whose raw increase is unfavorable use `+bad`; cadence uses `+good`. No speed
claim is permitted until the full matrix passes.
