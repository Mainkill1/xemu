# Vulkan hybrid cold-path trace

Set `XEMU_VK_HYBRID_TRACE` to an output file path before starting xemu. The renderer keeps up to 16,384 events in memory for the current guest frame and writes detailed records only when the interval between flip-stall frame boundaries reaches 50 ms. A `frame` row reports the number of retained and overwritten events. The trace is diagnostic; use an ordinary build without this setting for performance acceptance.

Each event row is:

```text
type,frame,draw,time_us,route,pipeline_hash,shader_hash,ticket,a,b,c,d
```

`time_us` and the timing fields below use the same monotonic clock. A zero hash means that identity is not available at that stage of the draw. On compile rows, `pipeline_hash` contains the shader-stage number and `shader_hash` identifies the GLSL source; those rows do not yet have a complete pipeline key. `route` is 0 for specialized and 1 for ubershader except on required-compile rows, where it records whether speculative compilation was active when the required job started.

| Type | Event | a | b | c | d |
| --- | --- | --- | --- | --- | --- |
| 1 | Required compile | submitted time | worker start | worker finish | caller return |
| 2 | Speculative compile | submitted time | worker start | worker finish | success |
| 3 | Completion adoption | adoption start | adoption end | published | metadata matched |
| 4 | Completion batch | count | batch start | batch end | reserved |
| 5 | Shader-binding probe | entry present | metadata ready | binding-key hash | reserved |
| 6 | Pipeline probe | complete pipeline ready | unchanged-key fast path | reserved | reserved |
| 7 | Graphics-pipeline creation | call start | call end | reserved | reserved |
| 8 | Route transition | previous route | selected route | selection epoch changed | shader state dirty |
| 9 | Renderer finish | finish reason | fence wait | queue-submit CPU | command buffer active |
| 10 | Resource shortage | shortage kind | index or requested size | capacity | route-specific detail |

For type 1, queue wait is `b-a`, compilation is `c-b`, and caller wait is `d-a`. Required/speculative overlap is the intersection of their `[b,c]` intervals, summed per required job. Pipeline creation time is `b-a` for type 7. The route decision still occurs before complete-pipeline readiness is established at this diagnostic head; probe rows measure the current behavior and do not claim that route selection is fixed.

This format is intended for cold attribution before changing route policy. Compare equal guest work and matched cache state; the act of serializing a slow frame can affect the following interval, so separate diagnostic builds from normal timing runs.
