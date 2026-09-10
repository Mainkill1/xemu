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

## Change boundary

The candidate will reduce first-use shader work by reusing validated shader
artifacts across equivalent `ShaderModuleCacheKey` values. It must preserve:

- complete vertex, geometry, and fragment key identity;
- Vulkan device ownership of every `VkShaderModule`;
- reference counts and eviction cleanup;
- uniform reflection data and layout identity;
- unsupported-driver and cache-miss fallback behavior;
- deterministic output with an empty or invalid persistent cache.

The first implementation step will split shader-module lookup, GLSL generation,
SPIR-V compilation/reflection, and Vulkan module creation time. Only the stage
that owns the measured cost will receive a behavioral change.

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
| Morrowind snapshot | Vulkan and OpenGL with verified Start/B, image transition, and guest progress |
| Performance | Average, p95, p99, maximum, and stalls |
| Resources | CPU, GPU, memory/cache growth, lifecycle, and process/private-disk cleanup |

Positive Improvement percentages are favorable. Timing and resource metrics
whose raw increase is unfavorable use `+bad`; cadence uses `+good`. No speed
claim is permitted until the full matrix passes.
