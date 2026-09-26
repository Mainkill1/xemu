# Shader Browser Stage 2: details, host artifacts, and portable export

This document defines the implementation contract for Stage 2 of #235. The branch is stacked on `feature/shader-browser-stage1`; it must be rebased onto the accepted Stage 1 result before merge.

## Scope

Stage 2 extends the read-only Shader Browser with:

- semantic inspection of the canonical guest shader recipe;
- resident or reconstructible host source inspection;
- existing host program/pipeline variant inspection;
- selected-shader lifecycle history;
- portable canonical recipe export.

Stage 2 does not implement replacements, overrides, preview rendering, prewarming, shader compilation on request, or a second persistence layer.

## UI

The existing right pane keeps its outer tabs:

- Shader Details
- Settings
- Live Preview

Shader Details gains nested tabs:

- Overview
- Guest
- Host
- Lifecycle

The shader-list context menu adds `Export recipe`. Export is disabled when canonical recipe bytes are unavailable.

## Data ownership

Stage 1 SQLite remains the only persistent catalog. Stage 2 adds one bounded selected-shader detail snapshot in memory. Generated host source, runtime handles, timings, lifecycle events, and pipeline objects are not written into the canonical shader record.

The selected detail service uses a newest-request-wins protocol:

1. XUI publishes a request ID, full ShaderKey, active backend, and requested detail masks.
2. The active renderer polls from a safe owner-thread point.
3. The renderer copies only already-resident data or deterministic source text that can be regenerated without compilation.
4. The renderer completes the request as Complete, Partial, Unavailable, or Failed.
5. Stale completions are rejected by request ID and ShaderKey.

## No-side-effect rule

Opening or refreshing details must not:

- compile or link a shader;
- create a module, program, or pipeline;
- prewarm a cache;
- mutate renderer LRU state;
- submit or wait for GPU work;
- read a framebuffer or texture;
- change the active specialized/uber route;
- perform SQL or file I/O on a renderer thread.

Missing data is reported as unavailable.

## Bounds

- One active selected-shader snapshot.
- Maximum generated source payload: 4 MiB per source record.
- Maximum text label/status/message: 4096 bytes.
- Maximum lifecycle records: 128, with an exact dropped counter.
- Newest unclaimed request only; rapid navigation cannot grow an unbounded queue.
- Source line offsets are rebuilt only when source generation/selection changes; visible lines are clipped with `ImGuiListClipper`.

## Portable export

Filename:

`<TitleID>-<ShaderHash>-<stage>.xemu-shader.json`

Schema: `xemu.shader-recipe-export.v1`

The export contains:

- identity and recipe format versions;
- title ID and full ShaderHash;
- stage;
- canonical recipe bytes encoded as base64;
- recipe byte count;
- sorted, deduplicated title/build associations.

It excludes generated host source, backend identity, GPU/driver data, process handles, performance statistics, lifecycle history, screenshots, and replacement policies.

## Backend wiring

### Vulkan

Return existing resident GLSL/module metadata, existing pipeline/binding variants, and lifecycle facts from the accepted Stage 1/hybrid infrastructure. Never call shader-module or graphics-pipeline creation APIs for a detail request.

### OpenGL

Return resident program metadata. Where Stage 1 retains an exact module key but not the source string, regenerate source text only and mark it `reconstructed`. Never create, compile, link, validate, or load a program for inspection.

## Merge gates

- malformed and unknown recipe versions fail safely;
- stale request completions are rejected;
- rapid selection retains only the newest request;
- source, label, message, variant, and lifecycle bounds are enforced;
- portable export round-trips to identical canonical recipe bytes and ShaderHash;
- Stage 1 catalog/session/artifact tests remain green;
- native OpenGL and Vulkan tests prove no detail-induced compile/program/module/pipeline creation;
- renderer reset, switch, save/load, and shutdown remain safe;
- representative output and guest progression remain unchanged;
- closed-window and visible-detail overhead are measured.
