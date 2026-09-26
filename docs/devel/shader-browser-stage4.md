# Shader Browser Stage 4 — isolated live preview

> **Status:** Draft stacked on Shader Browser Stage 3 at
> `8e03d756398b8299da464c17d064729db499f68a`. The immutable model,
> request governor, external Shader Browser window, and bounded private OpenGL
> and Vulkan pixel preview paths are implemented. Expanded interactive product
> requirements and gameplay overhead qualification remain required before merge.

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

- deterministic Quad, Sphere, or Cube geometry and compatible partner-stage outputs;
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

### One-shot captured draw and replay

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

The **Capture selected draw** button arms one request for the selected shader's
title/build scope, shader hash, backend, session, and renderer. It consumes at
most one matching draw after the renderer submits it. Selection or window
changes cancel the request, and an unanswered request expires. Merely opening
the browser does not record game draws. Current capture covers bounded CPU
inline vertices only; indexed draws and other vertex sources do not enter the
capture path. The draw path copies at most 1 MiB or 4096 vertices with a 2 ms
capture cap, and the sealed owned packet has a 32 MiB ceiling. It performs no
game GPU readback, wait, extra submission, file write, or cache operation.

The packet stores exact stage identities in the binding order (vertex or fixed
function, pixel, optional geometry), generated stage sources, CPU uniforms,
inline attributes and constants, primitive, viewport/scissor, surface extent,
and raw raster, color mask, and blend registers. OpenGL generates source from
the copied shader state after the gameplay draw; Vulkan copies the active
module source only while armed. The packet has a content digest and no renderer
pointers or GPU handles. Unsupported inputs are shown as a reason before
private replay starts.

The first admitted class is **Approximate**: the original GPU destination is
replaced by a transparent RGBA8 clear. If a draw uses GPU textures, its used
stages receive declared, opaque diagnostic 1×1 texels; blend operates against
the declared clear, and unwritten color channels retain it. The exact
substitutions are in the packet and the UI warning. Depth/alpha/stencil tests,
logic operations, culling, missing stage source, and unrepresentable primitives
remain Unsupported. Private GL and Vulkan execution of these captured packets
is the next replay step; a successful capture does not claim rendered game
pixels yet.

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
- automatic private preparation after paused selection settles, with an explicit
  paused preparation fallback;
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

Shader/scope/title/build/session/renderer changes, disable, and visibility loss
invalidate pending results. Ready outputs are discarded, display-leased outputs
enter Retiring, and active backend work may finish only to be rejected as obsolete.
Mode and source edits within the same display scope preserve Current and Reference
leases; only a new successful frame replaces Current. Acquiring a frame never
implicitly retires another lease: the HUD explicitly releases the prior Current.
Its sampling fence must signal before that texture slot can be reused.

### Honest UI scaffold

The Live Preview tab exposes:

- Enable preview, default off;
- Normal / Uber / Replacement / Visualize mode selection;
- state, pressure, update ceiling, and slot ownership status;
- selected shader/backend target;
- a preparation button enabled for a validated packet while paused;
- four editable synthetic corner colors, a 2 × 2 texture, UV scale/offset,
  sampler filter/wrap, combiner constant, fog color, and alpha reference;
- RGBA and individual color-channel tints, alpha opacity over a checkerboard,
  zoom, pan, and Freeze Reference for side-by-side comparison across modes;
- a private OpenGL output when an eligible pixel shader is prepared.

OpenGL preparation is automatic after the 150 ms selection debounce while the
guest is paused; Prepare while paused remains a fallback. The private
worker compiles the copied fragment source with a deterministic partner vertex
stage, draws into one of three preview-owned textures, and uses producer and
consumer fences before slot reuse. The linked program's active uniform
interface is checked before use: only synthetic scalar/vector/matrix inputs
and the four private 2D sampler bindings are admitted. Unsupported active
inputs are reported as `Unsupported` until the compile identity changes. Failed
compilations likewise retain their error without an automatic or button retry loop. The Vulkan path uses the same logical
slots and HUD presentation leases.

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

### Implemented Vulkan synthetic interface

`shader-browser-preview-vk.*` owns a separate Vulkan instance, logical device,
low-priority graphics queue, command pool, pipeline, RGBA8 target, fixture image,
and coherent staging buffers. It resolves Vulkan functions into its own dispatch
table and never changes the game renderer's global loader state. Compilation
uses private glslang objects with its own process-lifetime reference. No game
pipeline cache, descriptor, queue, or `PGRAPHVkState` enters this path.

The existing private GL worker claims Vulkan work with an explicit backend
filter. It polls the private Vulkan fence, reads only completed coherent staging
bytes, flips the rows to GL texture order, uploads into the service-selected GL
slot, and publishes Ready only after the GL producer fence completes. HUD
consumer fences retire sampled Vulkan presentation textures through the same
three-slot lease ring as OpenGL. There is no HUD-thread pixel upload or Vulkan
wait. Shutdown joins this private worker and retires its private device.

The supported interface is intentionally bounded:

- The copied fragment GLSL uses the exact generated synthetic partner stage.
  Fragment locations and formats must match that partner; output is location 0
  `vec4`, optionally with `gl_FragDepth`. Supported fragment built-in inputs are
  `gl_FragCoord` and `gl_FrontFacing`.
- Descriptor set 0 may contain one uniform block of at most 4096 bytes and four
  individual floating-point, non-shadow `sampler2D` descriptors named
  `texSamp0` through `texSamp3`. Binding numbers are reflected, not guessed.
- Uniform members must match the known pixel interface names, scalar types,
  dimensions, and array lengths. Reflected offsets and array strides are used
  for writes. Clip regions, clip range, surface scale, texture scale, combiner
  constants, fog color, and alpha reference have explicit synthetic values.
  Bump matrices/scales/offsets, color keys/masks, and depth factor/offset are
  explicitly zero synthetic values.
- Cube, array, integer, shadow, multisample, storage, and input-attachment
  resources; descriptor arrays; unknown uniform fields; push constants;
  incompatible varyings; and the uber control block return `Unsupported`.
  Compilation failure is reported separately. No shader is substituted.

The one active job reuses a 320-square private target and a 409600-byte staging
buffer for all accepted extents, including 160 and 320. A completed CPU buffer
is at most 409600 bytes, and the three GL textures follow service ownership.
These transport allocations are separate from the 32 MiB immutable packet
budget. Driver allocations and compiler memory are additional, implementation
and device dependent costs; no total host-memory bound is claimed.

The native test executable `test-xemu-shader-browser-preview-vk` is built with
Vulkan support but run explicitly on GPU hosts. It is excluded from the default
host unit suite so compile-only builders do not execute native GPU tests.

### Persistent synthetic clock and update policy

`OnDirty` is the default: a successful current result idles until source,
fixture, view, or output changes. `Continuous` uses a persistent private clock
with Play/Pause, Restart, time scrub, speed (0.05–8×), loop enable and length
(0.1–3600 seconds), and a clock sample counter. Restart resets time and the
counter. Disabling loop permits indefinite playback; there is no run-duration
cutoff. Clock samples count cadence emissions, not guest or presented frames.

The clock drives a deterministic eight-second synthetic cycle: horizontal UV
translation, half-speed vertical UV translation, and RGB vertex-color gain
between 0.3 and 1.0. Both private backends apply this to decoded preview-owned
fixtures. The UI labels this synthetic animation; original game time uniforms
are unavailable. Shaders that ignore these inputs can remain visually static.
The configurable loop wraps clock time; it does not rescale the fixture cycle.
`OnDirty` never applies the animation, including after switching from Continuous.

Time affects only result identity. Ticks reuse the immutable packet and prepared
shader, copying neither shader source nor recipes. One active job and the
three-slot lease ring remain the only work/output queue. A newer clock tick
allows an older in-flight sample to publish so slow GPU work cannot starve the
display. Source/input changes and explicit clock edits still reject obsolete
completions. Pausing requests one final sample, then stops clock-driven work;
editing or scrubbing while paused requests a frame immediately. A failed
unchanged attempt retries only at the governed cadence.

Paused guests target 30 Hz; running guests retain 15/8/4 Hz ceilings and the
health freeze rules. Hidden, disabled, unselected, unprepared, unhealthy, and
slot-exhausted previews freeze the clock. Admission resumes from a fresh time
anchor, without hidden elapsed time or accumulated work. The first resumed
clock-driven sample waits one cadence interval. User edits bypass cadence only
while the guest is paused; health and slot admission remain mandatory.

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
| Paused update target | 30 Hz |
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
The frozen comparison was exercised on the Deck by pinning one completed
slot, editing a corner, and observing a new result beside the frozen image
with two slots leased. Clearing the frozen frame returned to one lease.
Channel selection, zoom, and pan changed presentation without requesting a
new shader render. Closing the browser with both frames visible left the game
running; hidden-frame retirement continues through nonblocking fence polls on
the main presentation context until the logical leases can be released.

The private Vulkan native test passed on the Steam Deck's RADV VANGOGH device:
quad color readback, 2D texture inputs, fixture and sampler edits, 160/320 output,
the known pixel uniform block, unsupported interface rejection, compile failure
recovery, stop, and unavailable-device retry. The external browser also prepared
a resident Vulkan pixel shader to Ready, displayed its synthetic gradient,
pinned a frozen frame, and rendered an edited fixture beside it with two leased
slots. Closing the browser left the guest running; a monitor quit then exited
normally. These are bounded functional checks. Validation layers were not
available on that Deck image, and matched gameplay overhead was not measured.

The persistent clock's focused model, adapter, clock, service and native Vulkan
tests also passed on the Deck. Coverage includes 10,000 simulated continuous
frames, pause/scrub/restart, shared source ownership, stable compile identity,
slow-result publication, and freeze/resume without a time jump. A regression
specifically exercises the worker's alternating backend probes. The OpenGL UI
showed advancing samples and changing synthetic color output, then idle pause
and a restart at time/sample zero. The Vulkan UI likewise advanced and changed
output; paused scrubbing to 2.000 seconds produced a new frame and idled with
one leased slot. Both native UI processes exited cleanly. Vulkan's native pixel
test changes clock
time without preparing another program and verifies different output bytes.
These checks establish functional behavior, not an achieved refresh rate or
matched gameplay overhead.

Those checks do not establish:

- a Windows xemu build or native Windows operation;
- representative native Vulkan UI/lifecycle and performance qualification;
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

## Private scene geometry and camera

The synthetic scene now provides a two-sided Quad, a 32-by-16 tessellated Sphere,
and a six-face Cube. All vertices and face-local/spherical UVs are generated
privately. Both backends consume the same bounded CPU geometry and homogeneous
clip positions, preserving perspective interpolation. Vulkan converts clip Y
and depth to its viewport convention before the existing presentation row flip.
There are at most 3,072 vertices (408 KiB with the current fixture attributes) per generated mesh. Vulkan allocates
one fixed vertex buffer; GL uploads only the current bounded mesh.

Camera controls expose yaw, pitch, distance and two-axis pan, plus reset. Yaw
is bounded to +/-180 degrees, pitch to +/-85 degrees, distance to 2.5–12 units,
and pan to +/-2 units. Non-finite values become finite defaults. A 45-degree
perspective projection uses near/far planes 0.1/32. The camera stays outside
the closed convex meshes even at minimum distance.

Visibility is determined by rejecting camera-back-facing triangles of the
closed convex Sphere and Cube; the Quad reverses its winding when viewed from
behind. Their front surfaces cannot occlude each other, so this private
visibility method needs no shared depth buffer. This is an opaque synthetic
surface approximation: transparency, discard revealing back surfaces, custom
fragment depth and guest depth/blend interactions are not reconstructed.
Existing unsupported source-interface rejection remains in force.

Scene settings are bounded values owned by the immutable input packet and the
claimed result key. Camera/mesh edits update a small service-owned override and
view revision, preserving the source/recipe shared pointer and compile key.
Every claimed job snapshots the scene; completion after another camera edit is
stale. Paused interaction uses the clock task's immediate refresh path. Running
pressure admission, one active job, three output slots and the 32 MiB owned
packet payload cap remain in force. Clock fixture animation applies to every
mesh.

Focused CPU tests cover generated winding, finite clipping, Vulkan conversion,
camera movement, clamps and service result identity/source-storage reuse.
Deck native pixel tests render all three meshes and changed cameras: at 64x64,
OpenGL red-pixel coverage is 1270/1240/2517; at 32x32 Vulkan coverage is
316/312/633 (Quad/Sphere/Cube). Vulkan also checks animation on each mesh using
one prepared pipeline. The GL native test renders the production geometry and
synthetic partner interface in a private SDL GL context; it does not exercise
the asynchronous HUD executor lifecycle. These checks do not qualify Windows,
interactive UI cadence, transparency accuracy or gameplay overhead.

### Deterministic fixture profiles (Task 3b)

The private fixture encoding is versioned (`PFX`, version 2), exactly 172 bytes.
It retains profile identifiers, four independently selected texture patterns,
D0/D1/B0/B1, fog scalar/color, alpha reference, combiner constant, UV transform,
cube direction, legacy corner colors and editable flat texture quadrants. Invalid
versions, profiles, nonfinite inputs and out-of-range values fail closed. Editing
these bytes changes fixture digest/input revision, never source or compile key.
D0 additionally receives the corner-color modulation; D1/B0/B1 are independent.

Named profiles are Flat Color, UV Gradient, Checker/UV Grid, Alpha Gradient,
Multi-texture, Normal-like Direction, Cubemap/Direction, Fog/Depth Ramp and All
Inputs Diagnostic. Patterns are deterministic 8x8 RGBA8. Cube faces have six
contrasting axis colors (+X red, -X cyan, +Y green, -Y magenta, +Z blue, -Z yellow).
The direction and axis buttons select the preview sampling direction. The fog
profile uses a synthetic UV ramp as a depth-like input, not guest depth. Diagnostic
combines distinct texture stages and nonzero fog. Texture choices remain editable
per T0–T3 regardless of the suggested profile.

Suggestions decode the validated Stage 2 canonical pixel recipe: texture-stage
program, cube flags, alpha-test/kill flags and fog register references in combiner
inputs. Invalid/missing recipes receive no inferred suggestion. Explicit profile
selection overrides suggestions; source text is not used to infer recipe defaults.
Inputs unused by the resident shader have no effect. Integer, shadow, array,
multisample and storage samplers remain Unsupported. Supported `texSamp0`–`3`
interfaces use floating sampler2D or samplerCube; GL active uniform reflection and
Vulkan descriptor reflection determine image targets. Vulkan preserves binding,
array, uniform member offset/type and varying validation.

All texture data is preview-owned: at most four images, each at most six 8x8x4
faces, hence 6,144 bytes of texel payload. Vulkan retains a 6,144-byte upload
buffer; generation uses a single 1,536-byte scratch array. GL uploads the same
bounded generated data directly. Actual driver allocation granularity is separate
from these payload sizes. The packet retains only the 172 encoded bytes, charged
by vector capacity under the unchanged 32 MiB cap; there are no retained profile
libraries or borrowed texture pointers. The expanded 136-byte scene vertex gives
a bounded 417,792-byte vertex buffer (3,072 vertices). One active job and three
output slots are unchanged. No game queue/cache/framebuffer access is added.

Deck native readbacks cover each named profile, six-face cube allocation with
+Z/+X edits, distinct T0/T1 descriptors, independent color inputs, fog scalar/color,
combiner constants and alpha reference. Input edits reuse prepared Vulkan programs.
GL uses the focused native harness for the production shared generator/partner
contract; production GL worker/presentation smoke remains part of Task 6.

Sampler coordinate routing uses a preview vertex attribute populated from the
linked GL active-uniform types or Vulkan reflected descriptor types. It does not
parse sampler declarations from GLSL source; comments, whitespace and macros
therefore cannot switch a 2D input to cube coordinates or vice versa.

### Explicit Visualize channels (Task 3c)

Visualize offers Final RGBA and opaque grayscale Final R/G/B/Alpha from the
selected shader's actual private render target. Final RGBA retains its alpha for
checkerboard compositing. Final Alpha shows the stored shader output alpha as
brightness, including the private target's clear alpha outside rendered geometry;
it is not a discard/coverage measurement.

Additional channels are explicitly labeled preview-owned 2D fixture charts:
UV (clamped red/green), D0/D1/B0/B1 (opaque RGB), T0–T3 RGB texel atlases, Fog
(grayscale), a horizontal depth-like UV ramp, and an approximate alpha threshold
mask. The charts are independent of scene/camera and do not execute the selected
shader. D0 uses bilinear corner modulation of its fixture color, including its
alpha for the threshold mask. Fog follows the fixture profile's horizontal ramp
where applicable. Texture atlases show all six generated faces, bottom row
+X/-X/+Y and top row -Y/+Z/-Z; a 2D pattern repeats across faces. They show RGB
texels before sampler filtering/coordinate routing and do not expose sampled guest
texture registers. The depth-like ramp is neither scene nor guest depth.

The approximate mask compares fixture D0 alpha strictly greater than the fixture
alpha reference. It does not infer a guest alpha function or execute shader
discard. Exact shader discard mask is visibly Unsupported and disabled: output
RGBA cannot recover that coverage. Labels and provenance accompany channel
selection, and hovering each Current/Frozen channel label describes that frame's
own provenance.

Channels are result identity, not compile identity. A paused channel edit requests
an immediate governed update using the existing shared source packet and prepared
program. Running admission still follows the existing pressure ceilings. A stale
in-flight channel completion cannot replace a newer request. Channel edits also
reclaim obsolete Ready frames that have not yet been acquired, so they cannot be
newly presented after the edit. Frozen slots keep their original channel and pixels; the ordinary three-slot producer/consumer
fences and retirement protocol remain in force.

OpenGL uses per-output-texture swizzle for scalar output sampling, with no extra
shader, draw, or GPU readback. Vulkan transforms its already completed private
RGBA bytes. Diagnostic charts are bounded CPU RGBA8 buffers (at most 409,600
bytes each), uploaded only on the private worker through the existing producer
fence path. GL's temporary chart can coexist with the retained Vulkan transport
buffer; these bounded allocations are separate from the 32 MiB immutable packet
budget. No game cache, queue, framebuffer or wait is added. Charts currently
require successful preparation of the selected shader, just like final output.

Deck native checks sample GL final RGBA/scalars and uploaded chart pixels, and
exercise the production Vulkan executor's final channels and CPU charts without
re-preparation. They cover UV, D0, T0, Fog, depth-like ramp, approximate mask and
Unsupported discard. Focused tests cover all named channels/atlas faces,
provenance, bounds, compile/source reuse, immediate paused updates, stale active
jobs, frozen/current channel identity and all three slots returning to Free.
The GL native harness exercises the production swizzle/chart helpers; production
GL worker/HUD lifecycle, Windows, achieved UI cadence and matched gameplay
performance remain Task 6 qualification gates.


### Last-good images and preparation lifecycle

Current retains its last successful private image after packet construction,
compilation, interface, or rendering failure. STALE identifies the retained image;
its own mode, source digest, and replacement id/revision remain visible separately
from the attempted identity and current error. Input construction failures also
identify the source snapshot and replacement-store generation when no source is
available. A later successful completion replaces Current atomically. Obsolete
active work and completed-but-unacquired source edits cannot replace it.

Freeze Reference preserves that frame across Normal/Uber/Replacement changes for
the same shader and scope/session/renderer epochs. Reference and Current identify
their own origins. Freezing transfers a lease and requests a fresh Current; until
it arrives both views sample the same retained texture under one consumer fence.
Clearing Reference before an independent Current arrives transfers the sole
last-good lease back to Current, including its sampling fence; a failed edit
therefore cannot turn Clear Reference into a blank preview. Afterward the two
views use at most two leases. A third busy/retiring slot causes
preview drops, never a game wait or texture overwrite. Selection/epoch changes,
disable, closing/hiding the tab, and visibility expiry retire both images.

Automatic startup runs on the HUD thread after paused selection debounce; SDL
context creation is never moved into the worker. Preparation remains newest-only.
Running guests see **Pause required** when preparation is needed: a safe running
SDL shared-context initialization path has not been established. No game cache,
queue, renderer lock, or automatic pause is used to improve this UX.

Ordinary HUD retirement polls fences with zero timeout. Terminal HUD teardown
joins the private worker, waits at most one second in aggregate for remaining
consumer fences, then deletes output textures on the consuming HUD context.
External-window cleanup switches to the external browser GL context before
preview shutdown and restores the main context afterward; the embedded browser
shuts down in the main HUD context. A failed external-context switch is logged,
and cleanup explicitly restores and checks the shared main context before using
it as a fallback. Null saved window/context pairs are rejected instead of treating
SDL's successful unbind as a restored context, and remaining GL backend teardown
requires a real current context. If neither context is usable, preview shutdown
stops/joins the worker without HUD GL calls and leaves GL objects for terminal SDL/share-group
teardown; remaining ImGui GL cleanup is skipped with an error. The worker no
longer deletes those output textures. On timeout, GL object lifetime rules
preserve storage referenced by queued commands ([OpenGL 4.5, section 5.1.3](https://registry.khronos.org/OpenGL/specs/gl/glspec45.core.withchanges.pdf)).
This is terminal deletion, not a claim that an unsignaled lease has retired.
The service invalidates the entire backend epoch and advances slot generations;
a restart must publish fresh inputs and allocate new texture objects. No terminal
wait is used during normal preview work, tab close, disable, or mode changes.

The native `test-xemu-shader-browser-preview-lifecycle` harness links the production
asynchronous executor and ImGui GL renderer. Its OpenGL and `vulkan` variants
check actual red/green texture pixels through bad replacement/source and render
failures, recovery, cross-mode Reference, paused automatic startup, epoch reset,
disable, and terminal shutdown/restart. Focused service tests cover retained
attempt identities/errors, delayed consumer retirement, three-slot pressure,
source supersession, pre-packet failure, hide, and stale lease generations.
Windows qualification requires the exact published artifact; native Windows and
matched gameplay performance remain release gates.
