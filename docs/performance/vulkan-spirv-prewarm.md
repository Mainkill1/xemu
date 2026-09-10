# feature/vulkan-spirv-prewarm

**Status:** Investigating  
**Stable baseline:** `9f618d6d8c4c446ef023955f3d4de22f661f61a4` / retained product executable `3489fdcc593e942b92a612bf35a98f509ff0907e3370e1e5f45f2972d83fb16b`  
**Previous main:** `e18ba8d6274cf227cc9e5ae1b5684f28ed911a99`  
**Current candidate:** Pending implementation on this branch

## Summary

**Current result:** Cause confirmed; behavioral implementation and testing are pending.

**Headline:** The measured PGR2 hitch compiled four first-seen shaders and spent
18.217 ms in glslang. Reusing validated SPIR-V loaded before gameplay can
remove that compilation from a warm run without changing shader state.

**Next:** Implement a bounded, versioned cache with no gameplay-path file I/O,
then compare cold, warm, and same-process runs before starting the complete
product matrix.

## Investigation

### Why this patch exists

The completed PR #68 diagnostic recorded 141 Vulkan stage-module misses and
463.419 ms of glslang work during one PGR2 snapshot run. The primary hitch at
guest frame 673 contained two vertex and two fragment misses. All four were
new generated sources in that process and consumed 18.217 ms in glslang.

Twenty other misses regenerated stage/source identities already seen under a
different state key, costing another 68.200 ms. Key normalization is useful
follow-up work, but it cannot remove the four first-seen compilations in the
primary hitch.

### Patch hypothesis

Load a bounded SPIR-V artifact index during Vulkan renderer initialization.
On a module-cache miss, generate the exact GLSL as today and look up a record
identified by stage, exact source bytes, shader-generator/cache schema, target
environment, and compiler options. A validated hit supplies SPIR-V from
memory; a miss uses glslang and queues the resulting artifact for a single
producer-quiesced, atomic write outside gameplay.

The first candidate intentionally retains GLSL generation, Vulkan module
creation, and reflection. The measured primary frame spent 0.797 ms generating
GLSL, 0.043 ms creating modules, and 1.269 ms reflecting them, compared with
18.217 ms compiling. Precreating complete modules is a later step if warm-run
tail measurements show the remaining work matters.

Risks include accepting stale or corrupt cache data, confusing compiler and
generator identities, unbounded disk/RAM growth, file I/O on the draw path,
incorrect SPIR-V lifetime, and failed-cache behavior that prevents the normal
compiler fallback.

## Processing flow

### Current / before

```mermaid
flowchart LR
    A[Shader module state-key miss] --> B[Generate exact GLSL]
    B --> C[Run glslang synchronously]
    C --> D[Create Vulkan module and reflect uniforms]
    D --> E[Resume the blocked draw]
```

### Candidate / after

```mermaid
flowchart LR
    P[Renderer initialization] --> Q[Read and validate bounded cache once]
    A[Shader module state-key miss] --> B[Generate exact GLSL]
    B --> C{Exact in-memory artifact hit?}
    Q --> C
    C -->|Yes| D[Reuse validated SPIR-V]
    C -->|No| F[Run glslang and queue artifact]
    D --> G[Create Vulkan module and reflect uniforms]
    F --> G
    G --> E[Resume the draw]
    E --> H[Producer-quiesced atomic cache write at shutdown]
```

### Processing difference

| Area | Current | Candidate | Expected effect |
| --- | --- | --- | --- |
| Draw-path compilation | glslang on every process-first source | Memory lookup on a valid warm hit | Remove measured compiler stall |
| File I/O | None | Initialization read and shutdown write | No gameplay-path file I/O |
| GPU work | Create the same Vulkan module | Create the same Vulkan module from identical SPIR-V | Equivalent GPU input |
| Memory | In-process modules only | Bounded validated artifact index | Measured and capped increase |
| Failure | Compilation failure is visible | Invalid/missing cache falls back to the same compiler path | Preserve current behavior |

## Planned code changes

- Add a small cache format with checked sizes, record counts, integrity, and a
  compatibility identity that is deliberately invalidated when generation or
  compiler policy changes.
- Split GLSL-to-SPIR-V compilation from Vulkan module construction so cached
  bytes and freshly compiled bytes use the same creation/reflection path.
- Load and validate cache data before gameplay; perform only in-memory lookup
  after GLSL generation.
- Queue new immutable artifacts in memory and persist them once during clean,
  producer-quiesced shutdown using temporary-file replacement.
- Expose hit, miss, rejection, byte, and fallback counters for qualification
  without synchronous hot-path logging.

**Main code path:** `hw/xbox/nv2a/pgraph/vk/glsl.c`, `shaders.c`, Vulkan
renderer lifecycle, and a focused cache-format module.

## Profiling

| Measurement | Stable baseline | Previous main | Current candidate |
| --- | ---: | ---: | ---: |
| PGR2 primary-frame glslang | Retained run does not contain this diagnostic | 18.217 ms diagnostic target | Pending |
| Full-run glslang | Retained run does not contain this diagnostic | 463.419 ms diagnostic target | Pending |
| First stage/source identities | Not recorded | 121 | Pending |
| Different-key/same-source compiles | Not recorded | 20 | Pending |

The instrumentation result identifies the cause; it is not a baseline or
candidate performance qualification.

## Performance results

Every percentage is Improvement %: positive is favorable and negative is
unfavorable. Interval, CPU-time, memory, and stall metrics are `+bad`; FPS and
completed work are `+good`.

| Workload | Renderer | Metric | Raw + | Stable | Previous main | Candidate | Improvement vs stable | Improvement vs previous main |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| PGR2 snapshot warm | Vulkan | maximum interval | `+bad` | Pending reuse | Pending reuse | Pending | N/A | N/A |
| PGR2 snapshot warm | Vulkan | primary-frame glslang | `+bad` | N/A | 18.217 ms diagnostic | Pending | N/A | N/A |
| PGR2 full start | Vulkan/OpenGL | average, p95, p99, maximum, stalls | `+bad` | Pending reuse | Pending | Pending | N/A | N/A |
| Morrowind snapshot | Vulkan/OpenGL | average, p95, p99, maximum, stalls | `+bad` | Pending reuse | Pending | Pending | N/A | N/A |

## XISO results

| Gate | OpenGL | Vulkan |
| --- | --- | --- |
| Focused cache format and failure injection | Not needed | Not run |
| Full catalog and output oracle | Not run | Not run |
| Validation errors | Not needed | Not run |

## Resource results

| Metric | Raw + | Stable | Previous main | Candidate | Improvement vs stable | Improvement vs previous main |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Cache RAM bytes | `+bad` | 0 | 0 | Pending | N/A | N/A |
| Cache disk bytes | `+bad` | 0 | 0 | Pending | N/A | N/A |
| Startup cache-read time | `+bad` | 0 | 0 | Pending | N/A | N/A |
| Shutdown cache-write time | `+bad` | 0 | 0 | Pending | N/A | N/A |

## Correctness

The same GLSL must produce byte-identical accepted SPIR-V on cache miss and
subsequent hit for the pinned compiler policy. Corrupt, truncated, oversized,
unknown-version, incompatible, and unwritable cache cases must fall back to
normal compilation without losing a draw or leaving partial state. OpenGL must
remain unchanged.

## Validation status

| Test | OpenGL | Vulkan |
| --- | --- | --- |
| Targeted patch and corruption/failure tests | Not needed | Not run |
| Cold/warm/same-process shader corpus | Not needed | Not run |
| Profiling comparison | Not needed | Not run |
| Resource comparison | Not needed | Not run |
| Morrowind snapshot | Not run | Not run |
| PGR2 full start | Not run | Not run |
| PGR2 snapshot | Not run | Not run |
| Full XISO | Not run | Not run |
| Final visual validation | Not run | Not run |

A single valid unexplained regression over 2%, including p95, p99, maximum,
or stalls, keeps the candidate on hold.

## Tradeoffs and decision

**Result:** Continue investigation.

No performance improvement is claimed. The initial focused gate must prove
that a warm hit removes glslang work, cache failures preserve output, and
startup/shutdown costs and storage remain bounded. Only then does the exact
candidate proceed to the full XISO, PGR2 full-start and snapshot, and
Morrowind snapshot matrix on both renderers.

## Evidence

- [Complete shader identity report](https://github.com/Mainkill1/xemu-perf-tests/blob/e77436eb3f7084bd8e406e07ede7ec5f318d1e66/docs/evidence/pr68-shader-identity-20260910/classification/REPORT.md)
- [Repository workflow](../repository-workflow.md)
- [Performance PR template](../../evidence/wiki-xiso-per-test/PERFORMANCE_PR_TEMPLATE.md)

## Final summary

The diagnostic proves the primary PGR2 hitch spends 18.217 ms compiling four
first-seen shaders. This branch will test bounded, validated, preloaded SPIR-V
reuse as one focused behavioral change. Implementation, correctness, resource,
and performance results are pending.
