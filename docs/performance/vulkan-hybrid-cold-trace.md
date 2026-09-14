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
| 5 | Shader-binding probe | entry present | metadata ready | binding-key hash | pre-route candidate probe |
| 6 | Pipeline probe | complete pipeline ready | unchanged-key fast path | shader metadata ready for pre-route probe | pre-route candidate probe |
| 7 | Graphics-pipeline creation | call start | call end | reserved | reserved |
| 8 | Route transition | previous route | selected route | selection epoch changed | shader state dirty |
| 9 | Renderer finish | finish reason | fence wait | queue-submit CPU | command buffer active |
| 10 | Resource shortage | shortage kind | index or requested size | capacity | route-specific detail |
| 11 | Uncovered route | specialized binding ready | specialized pipeline ready | fallback binding ready | bit 0: fallback pipeline; bits 1–2: fallback resources (0 ready, 1 rollover, 2 unavailable); bit 3: controls supported |
| 12 | Background pipeline submit | submit status | free pipeline-cache entries | reserved | reserved |
| 13 | Background pipeline adoption | submitted time | worker start | worker finish | adopted |
| 14 | Warm SPIR-V module materialization | call start | call end | cached SPIR-V bytes | module-key hash |
| 15 | Pipeline-layout creation | call start | call end | reserved | reserved |
| 16 | Render-pass lookup or creation | call start | call end | color format | depth/stencil format |

For type 1, queue wait is `b-a`, compilation is `c-b`, and caller wait is `d-a`. Required/speculative overlap is the intersection of their `[b,c]` intervals, summed per required job. Pipeline creation time is `b-a` for type 7.

Current Hybrid On routing requires both the exact shader binding and the complete graphics pipeline. A `d=2` probe row records the current candidate check; `d=1` identifies the older diagnostic-only pre-route check. A fallback pipeline hit with missing shader metadata or resources is not an executable fallback. A module completion alone cannot cause a specialized takeover.

An uncovered draw builds the route whose shader binding is already prepared. When neither binding exists, it builds the normal specialized route for the current draw and queues preparation of the reusable fallback family for a later frame. A complete fallback stays selected through temporary descriptor or staging rollover. Stable fallback draws reuse the current executable, compare only raw combiner constants, and revisit promotion at most once per 16 ms.

Pipeline-result polling checks an atomic empty-queue hint before acquiring the worker lock. Background pipeline submission leaves the existing cache entries intact. Only a completed pipeline seeks a safely evictable entry; if none is available, publication waits until one becomes available. A warm SPIR-V hit can still materialize a shader module on the renderer thread, and preparing a background pipeline still creates its layout and may create a render pass there. Types 14–16 attribute those costs separately from the worker's graphics-pipeline creation.

Compare equal guest work and matched cache state; serializing a slow frame can affect the following interval, so use a trace-disabled build for timing acceptance.
