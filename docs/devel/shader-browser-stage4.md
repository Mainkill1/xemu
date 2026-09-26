# Shader Browser Stage 4 — isolated live preview

> **Status:** Draft stacked on Shader Browser Stage 3 at
> `8e03d756398b8299da464c17d064729db499f68a`. The immutable model,
> request governor, external Shader Browser window, and private OpenGL pixel
> preview path are implemented. Vulkan execution and gameplay overhead
> qualification remain required before merge.

## Goal

Add a useful selected-shader preview without changing the route used by the
running game and without making gameplay wait for preview compilation,
submission, completion, presentation, or resource reuse.

The preview is optional and defaults off. When resources or scheduling time are
not safely available, the preview drops or freezes its own update.

## Dependency and branch rules

Stage 4 consumes the accepted interfaces from Stages 1–3:

- Stage 1 owns canonical shader identity, title/build associations, the browser,
  and the optional persistent catalog.
- Stage 2 owns semantic inspection and copied resident details. Stage 4 does not
  add execution work to the Stage 2 detail service.
- Stage 3 owns replacement packages and runtime override policy. Stage 4 may
  inspect/acquire immutable replacement payloads but never activates an
  override in the game.

This branch is stacked on `feature/shader-browser-stage3`. Rebase it onto the
final Stage 3 head before each native validation pass and retarget only after
the dependency chain merges.

## Non-negotiable protection contract

Normal gameplay must never:

- wait for a preview worker, compiler, queue, fence, output slot, or HUD lease;
- submit work to make the preview progress;
- compile/link/create a game shader, module, program, or pipeline for preview;
- change shader route, override policy, cache recency, prewarm history, or
  session statistics because preview is open;
- expose mutable guest memory, renderer-owned cache pointers, GL names, Vulkan
  handles, descriptor objects, or live texture bindings to XUI;
- perform SQL, file I/O, JSON parsing, ID formatting, or source hashing in an
  ordinary draw because preview is enabled;
- read the game framebuffer or take screenshots automatically.

A separate context, logical device, queue, or thread still shares physical host
resources. Those mechanisms do not prove harmlessness. Safety comes from
private ownership, strict bounds, conservative admission, and native evidence.

## Product meaning

### Synthetic preview — first deliverable

The initial image executes a selected pixel/fragment shader against explicit,
preview-owned inputs:

- a deterministic quad and compatible partner-stage outputs;
- generated textures and samplers;
- editable colors, alpha, UVs, constants, and view controls;
- a private render target and private destination contents.

The UI must label this output:

```text
Synthetic inputs — not an in-game draw
```

A shader recipe alone does not reproduce its appearance in a title. Missing
partner stages or unsupported interfaces return `Unsupported`; they are not
silently replaced with a generic shader and called exact.

### Supported replay — later deliverable

Replay consumes an already-owned immutable packet. It must not enable a
continuous capture path merely because the tab is open. A replay packet needs
all inputs required by the draw, including partner stages, geometry, constants,
texture subresources, samplers, raster state, and destination color/depth data
when those values affect the result.

Every replay is classified as:

- `Complete replay` — all required inputs and behavior are represented;
- `Approximate replay` — a declared substitution changes execution conditions;
- `Unsupported` — a required dependency cannot be represented safely.

Unsupported packets are rejected before backend work is admitted.

## Mode semantics

The existing dropdown remains:

| Mode | Private preview behavior |
|---|---|
| Normal | Original compatible shader behavior against preview inputs. |
| Uber Shader | Supported uber equivalent using the same private inputs. It does not change the game route. |
| Replacement Shader | Selected Stage 3 payload compiled and executed privately. It does not activate the rule in gameplay. |
| Visualize | Available output channels or known fixture inputs. It does not invent unavailable intermediates. |

Stage 4 v1 intentionally accepts pixel/fragment shaders first. Vertex,
geometry, and fixed-function rows remain inspectable and report the unsupported
preview boundary.

## Implemented foundation

### Immutable identity and packet model

`shader-browser-preview-model.*` defines:

- full `ShaderScope` plus `ShaderKey` selection identity;
- session and renderer epochs;
- backend and requested preview mode;
- separate compile and result keys;
- replacement ID/revision plus source content identity;
- input/view revisions, extent, packet kind, and replay classification;
- deterministic source, partner, and fixture digests;
- a validated immutable packet with canonical recipe bytes.

Compile identity intentionally excludes ordinary input/view revisions so a
uniform edit can render another image without recompiling. Result identity
includes those revisions and fixture content.

Validation currently enforces:

- explicit TitleID and concrete pixel stage;
- nonzero identity, generator, interface, and recipe versions;
- canonical recipe → selected ShaderHash round trip;
- source, partner-stage, and fixture bytes matching their supplied content digests;
- replacement mode carrying immutable source identity;
- 4 MiB per source, 8192-byte recipe, and 32 MiB aggregate packet limits;
- 1–320 pixel dimensions;
- replay packets owning inputs and declaring Complete or Approximate status.

Changing bytes under the same file name or caller revision cannot reuse a stale
compile/result key unless the validated digest is also unchanged.

### Newest-only request service

`shader-browser-preview-service.*` is a process-lifetime, thread-safe service
with no renderer pointers. It provides:

- preview disabled by default;
- explicit visibility heartbeat that fails closed when the tab stops drawing;
- explicit selected shader and immutable packet publication;
- 150 ms selection debounce;
- one pending newest request and one active job;
- stale completion rejection by request and full result identity;
- explicit preparation request only while the guest is already paused;
- no automatic pause command;
- immediate response to increased pressure and a two-second recovery window;
- stale gameplay-health rejection;
- static images rendered once until result identity changes;
- 15 / 8 / 4 Hz running ceilings for Normal / Elevated / High pressure;
- Critical pressure freezing preview;
- aggregate accounting across pending and active packet ownership.

Packet admission creates service-owned immutable storage. The 32 MiB limit
counts retained buffer capacity, and validation rejects over-limit packets
before digesting source or fixture bytes. Ready-frame recency has its own
completion sequence; the slot generation remains a lease nonce. Heartbeat
expiry follows the same invalidation path as an explicit tab close.

The Live Preview tab now assembles a scoped packet only when selection,
resident-detail generation, or replacement generation changes.
It copies the canonical recipe, selected resident fragment source or selected
Stage 3 replacement payload, a deterministic synthetic vertex stage, and a
four-corner color fixture. Missing source or incompatible replacement remains
unavailable; the tab does not ask the game renderer to compile anything.

The service emits pure `PreviewWorkItem` records. A later backend claims work,
performs it with private resources, and completes by token. The service itself
contains no GL/Vulkan API call.

### Output ownership

The foundation owns exactly three logical output slots:

```text
Free -> Rendering -> Ready -> DisplayLeased -> Retiring -> Free
```

A new update is dropped when no slot is free. The service never waits or grows
the ring. When the UI selects the newest completed frame, older Ready frames
that were never sampled are reclaimed immediately. Producer completion does not
make a sampled slot reusable; a later HUD/backend hook must provide the
consumer-retirement proof.

Selection, mode, disable, and visibility changes invalidate pending results.
Ready outputs are discarded, display-leased outputs enter Retiring, and active
backend work may finish only to be rejected as obsolete and retired safely.

### Honest UI scaffold

The Live Preview tab exposes:

- Enable preview, default off;
- Normal / Uber / Replacement / Visualize mode selection;
- state, pressure, update ceiling, and slot ownership status;
- selected shader/backend target;
- a preparation button enabled for a validated packet while paused;
- four editable synthetic corner colors, a 2 × 2 texture, UV scale/offset,
  sampler filter/wrap, combiner constant, fog color, and alpha reference;
- a private OpenGL output when an eligible pixel shader is prepared.

OpenGL preparation is explicit and requires the guest to be paused. The private
worker compiles the copied fragment source with a deterministic partner vertex
stage, draws into one of three preview-owned textures, and uses producer and
consumer fences before slot reuse. The linked program's active uniform
interface is checked before use: only synthetic scalar/vector/matrix inputs
and the four private 2D sampler bindings are admitted. Unsupported active
inputs are reported as `Unsupported` until the compile identity changes or
preparation is explicitly retried. Vulkan output is still unavailable.

### Separate Shader Browser window

Start xemu with `-shader-browser-window` (or `--shader-browser-window`) to open
the Shader Browser in a second operating-system window. The game stays in its
own window. The browser uses an independent ImGui context and GL presentation
context, and SDL events for that window do not reach guest input. Closing the
browser through the desktop window manager hides it while xemu keeps running;
the Debug > Shader Browser menu item can show it again. Without the launch
option, the browser remains in the main HUD.

The Live Preview tab publishes the actual guest pause state. A small atomic
flip count and interval, updated at the NV2A flip boundary, feed a conservative
health classifier; missing or stale flips freeze running preview admission.
Renderer initialization, switch, and shutdown advance a separate atomic epoch
so a packet cannot survive a renderer lifecycle transition under the same
identity. These publishers do not issue preview work by themselves.

## Remaining backend contract

### Immutable packet adapter

The low-frequency adapter outside ordinary draws copies:

- the accepted canonical recipe and recipe version;
- selected title/build scope and session/renderer epochs;
- generator/interface ABI;
- deterministic synthetic fixture inputs and a stable private vertex stage;
- immutable selected Stage 3 replacement source in Replacement mode.

It validates and moves a `PreviewPacket` into the service. It must not borrow
Stage 2 source vectors or Stage 3 payload memory beyond their ownership window.

### Pause and health publishers

Publish guest pause state and a small coarse health snapshot at existing owner
boundaries. Do not add per-draw timers. The minimum health record is timestamp,
pressure class, and whether game progress is healthy. Missing/stale health
means no running preview submission.

### OpenGL backend

Use preview-owned resources and a separately owned execution namespace. Reuse
pure shader generation and recoverable compiler helpers, not the Stage 3 game
program builder or cache because those functions use game partner modules,
cache policy, uniform readers, and use clocks.

Verify the actual platform context rules before moving context creation or
`MakeCurrent` to a worker. The initial backend may perform narrowly bounded
owner-thread operations while the guest is paused, provided normal gameplay
never waits for preview.

### Vulkan backend

Use a small private logical device/queue/resource set or another isolated
execution owner. Do not instantiate a second `PGRAPHVkState`, call the display
path, use the game pipeline cache, or submit through the game command stream.

Start with a reference transport:

```text
private image -> asynchronous private staging -> completed CPU bytes
              -> preview GL presentation slot
```

Only process completed data. External-memory interop is an optional later fast
path and must preserve device/format/synchronization compatibility.

### HUD lease retirement

After the actual ImGui OpenGL submission, associate the last sampled preview
texture with a consumer-completion primitive. Only after that primitive signals
may `CompleteDisplayRetirement()` free the logical slot and its backend object.
CPU command construction or a texture-ID swap is not sufficient proof.

## Initial resource limits

| Item | Initial limit |
|---|---:|
| Default state | Off |
| Full synthetic target | 320 × 320 |
| Reduced target | 160 × 160 |
| Running update ceiling | 15 Hz |
| Elevated pressure | 8 Hz |
| High pressure | 4 Hz |
| Critical/stale health | Frozen |
| Selection debounce | 150 ms |
| Pending requests | One newest |
| Active backend jobs | One |
| Output slots | Three |
| Retained packet buffer capacity | 32 MiB aggregate across pending and active work |
| Source text | 4 MiB per backend payload |

Driver/device allocations must be reported separately; application accounting
is not a claim about total host memory.

## Ordered handoff

1. Rebase onto final Stage 3 and keep the PR draft.
2. Preserve the model/service tests while adding the immutable recipe adapter.
3. Publish pause and coarse game-health state without adding draw-path timing.
4. Add a fake backend integration test proving work claim/completion and stale
   retirement across renderer/title/session changes.
5. Implement the private OpenGL synthetic pixel path first.
6. Add the HUD consumer-retirement hook and prove three-slot reuse.
7. Implement the Vulkan private reference path and async presentation copy.
8. Add fixture editing, channel visualization, zoom/pan, and frozen comparison.
9. Add the supported replay consumer only after synthetic preview qualifies.
10. Run native output, lifetime, resource, and frame-time qualification.

Do not combine live capture, persistent replay storage, external-memory
optimization, and both native backends into the first implementation step.

## Required tests

### Focused host tests

Maintain tests for:

- canonical recipe/hash mismatch;
- source and fixture digest mismatch;
- title/build/session/renderer identity separation;
- compile key stable across input-only edits;
- result key changes across input/view/fixture edits;
- newest-only A/B/C publication;
- stale preparation/render completion rejection;
- explicit paused preparation;
- visibility heartbeat expiry;
- pressure throttling and delayed recovery;
- static render-once behavior;
- no-free-slot drop;
- display retirement generation checks;
- active + pending packet memory cap;
- concurrent status and health publication.

### Native lifecycle tests

Cover:

- rapid selection and mode changes;
- title/build changes with the same ShaderHash;
- replacement edit under the same path;
- save/load, reset, renderer recreation/switch, and shutdown;
- preview hidden/closed while work is active;
- producer completes after invalidation;
- HUD completion delayed beyond multiple frames;
- all three slots leased or retiring;
- device/context creation failure and backend loss;
- output resize and presentation-transport failure.

### Native correctness

Compare game output before the HUD overlay, guest progression, reports,
effective override route, cache statistics, and validation output with preview
off/on. The preview panel itself intentionally changes the final composed
window and is not the game-output oracle.

### Performance method

Use matched Stage 3 and Stage 4 builds plus off/on roles within the same Stage 4
binary. Establish A/A noise first, then order-balanced comparisons. Separate:

- cold preparation while paused;
- ready static preview;
- ready animated 15/8/4 Hz preview;
- OpenGL and Vulkan reference transport;
- CPU-limited, GPU-saturated, and shader-heavy loading scenes.

Record frame-time p50/p95/p99/max, long-frame counts, guest progress, audio
underruns, process/thread CPU, GPU work, resident memory, preview claims,
drops, slot pressure, transport bytes/time, and exact source/binary/settings.

Proposed ready-preview acceptance targets are no more than 1% median frame-time
regression and p99 regression no greater than the smaller of 2% or 0.25 ms,
with a predeclared paired-analysis confidence bound. These are merge gates, not
results claimed by this draft. Inconclusive evidence does not enable running
preview by default.

## Current verification boundary

The foundation has been exercised in a focused host harness with strict GCC and
Clang compilation, ASan/UBSan, TSan concurrency coverage, and Clang static
analysis. The current Linux xemu executable compiles. On a Steam Deck using
Mesa OpenGL, the launch option opened separate game and browser windows, the
browser selected a resident pixel shader, and private preparation reached a
Ready output with a leased presentation slot. The selected shader rendered a
four-corner color gradient; changing one corner produced a new private result
without changing the running game. A normal window-manager close hid the
browser while the game kept running. A normal process shutdown with the private
worker active exited cleanly. The focused service test passed on the Steam
Deck after adding persistent Unsupported classification, and the same resident
shader still rendered under the uniform-interface check. This is a functional
smoke test, not a representative shader-correctness or performance
qualification. A later Deck run exercised the expanded fixture: changing one
2 × 2 texture texel and the UV offset each produced a new Ready result while
the output slot remained bounded. The focused fixture adapter test passed on
the Deck, including malformed and non-finite packet rejection.

Those checks do not establish:

- a Windows xemu build or native Windows operation;
- native Vulkan operation;
- representative shader output correctness;
- gameplay frame-time neutrality;
- HUD texture retirement on a real driver.

The PR remains draft until those native gates are satisfied.

## Agent handoff

Before claiming implementation completion, the next agent must publish:

- exact rebase parent and executable hashes;
- focused and full build/test commands with outputs;
- native GL and Vulkan capability/failure results;
- output and lifecycle evidence;
- matched gameplay-performance evidence;
- any backend/transport left paused-only or unsupported.

Do not convert missing evidence into a permissive default or silently route
preview through the game renderer.
