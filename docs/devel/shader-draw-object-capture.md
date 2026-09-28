# Shader draw, geometry, and object capture

**Partial live capture and approximate preview.** The workbench can arm one
request for the next submitted draw using the selected pixel shader. It resumes
a paused guest, copies supported triangle geometry, pauses again, and displays
that draw in the private OpenGL or Vulkan preview. The Synthetic scene remains
selectable. The game draw mode uses the captured geometry with synthetic
textures, constants, and vertex outputs; it is not exact material replay.

The current geometry decoder supports triangle lists with float3/float4
attribute 0 positions from the backend's resolved host buffers, using draw
arrays, inline indices, expanded inline float4 vertices, or packed float streams.
Each triangle-list range discards its own incomplete trailing vertices. Other
formats and topologies produce a metadata-only result. Capture copies at most 4096 positions and 12288 indices. The private
preview displays as many complete triangles as fit its 4096-vertex scene
budget after reference geometry. The UI offers index-connected parts as
inspection hints when a draw has multiple islands; these are not engine object
identities. No game asset name or cross-draw object assembly is inferred.

## Concept: one shader is not one model

```text
Selected shader
  -> exact draw events using that shader
     -> geometry segments -> proposed or confirmed object membership
     -> material inputs: shader stages, textures, samplers, constants, state
     -> resource versions -> earlier producers and later consumers
```

Keep three relationships separate in code and in the future UI:

| Relationship | What it establishes | What it does not establish |
|---|---|---|
| Shader use / draw / segment | Which submission and primitive selection we captured | The original engine model name |
| Object membership | Which segments the user or an engine-aware adapter confirmed belong together | Membership merely from equal shaders, textures, or hashes |
| Resource dependency | A captured producer version is an input to another draw | Object ownership or proof that a particular pixel changed |

One draw may batch multiple objects. One object may require multiple draws,
materials, and passes. The same mesh may be used by separate object instances.
A screen-space quad may process a picture containing the whole scene without
being the geometry of all those scene objects.

## What the code implements

`ui/xui/shader-browser-draw-capture.hh` defines the shared records and APIs.
The implementation is split into draw identity/admission, triangle-list
segmentation, relationship analysis, object grouping, and resource dependencies.
The analysis production units have a focused Meson test. A separate one-shot
request service and C bridge connect the final OpenGL/Vulkan submission points
to the workbench. An atomic claim reserves the exact request before geometry
copying; completion uses that token after the command is emitted. Cancellation
and re-arming cannot assign an older claimed command to the newer request.
Suppressed commands never claim or satisfy a request. Captures have a submission
serial so a Vulkan subdraw or mid-scope flush has its own identity.

OpenGL reads the position and index buffers actually bound to its command.
Vulkan reads its resolved inline staging generation, including private vertex
backing and expanded inline streams. While a request is armed, bounded float
position streams with a vertex prefix of at most 4096 vertices are preserved
before reservation and descriptor preparation and used by both the actual draw
and capture. Fixed RAM without this readable generation remains metadata-only.
This diagnostic path can add capture-time copying or OpenGL readback stalls;
the private preview render cadence is independent of that one-shot cost.

### Exact draw and segment references

`DrawEventKey` identifies session, renderer epoch, frame, draw number, and submission serial.
`DrawCaptureSummary` also retains title/executable scope, shader identities,
counts, typed resource touches, and segmentation metadata. Queries search the
provided capture collection; the future UI must supply the selected title/build
and capture scope, not concatenate unrelated libraries with unremapped IDs.

`DrawSegmentKey` identifies a selection within that draw. `SegmentTriangleList`
returns index-connected islands, original primitive IDs, degenerate primitives,
and incomplete trailing-index counts. It does not guess positions, weld UV
seams, or turn an island into a proven object. Noncontiguous selections retain
`primitive_indices`; do not replace `{0, 2}` with the contiguous range `0..2`.
Other topologies and instance/transform-based splits are future decoder work.

Geometry with unknown meaning, a screen-space draw, or an unresolved suspected
batch remains in `unresolved_segments`. It is still a shader seed and can have
resource dependencies; it is not quietly counted as one reconstructed model.
An unsegmented known geometry draw is only a whole-draw candidate, not proof
that it contains exactly one object.

### Membership versus inference

Matching geometry, transform, skinning, bounds, and submission adjacency are
inspectable evidence. They produce suggestions, never automatic ownership.
`SameObjectPass` is the name of a repeat/pass *candidate*, not a proven fact.
Bounds are compared only when their nonempty coordinate-space digests match.

Only equal, nonzero `confirmed_object_id` values consolidate segments, within
the same title/build/session/renderer/frame. IDs must come from explicit user
confirmation or verified engine metadata, not from hashes. Different confirmed
IDs keep repeated instances separate. Confirmation can join passes using
different camera transforms; an inferred transform mismatch merely blocks the
heuristic, not an already confirmed relationship. Membership is capture-local,
not a persistent engine asset ID. The confirmation UI is not implemented yet.

### Resource versions: inputs and downstream influence

`ResourceTouch` stores read and write versions separately. `storage_id` identifies
a capture-local backing allocation incarnation; `storage_range` is a canonical
byte range in that backing, independent of the guest pointer or texture view.
A render-target write and a texture read may use different resource kinds,
formats, guest addresses, and descriptors while referring to the same backing.
The capture adapter must establish that alias; this model does not guess it.

`BuildResourceDependencies` joins an earlier producer to a consumer only when
scope/epoch, backing ID, version, and overlapping canonical byte ranges agree.
It reports unknown versions, invalid ranges, missing producers, partial coverage,
and ambiguous overlapping producers. Read/read sharing and bind-only metadata
create no producer edge. `DerivedFrom` metadata alone also creates no edge;
a concrete consumed version must be represented as a read.

A read/write consumes version A and produces version B. Partial updates must be
represented as provenance fragments: one read touch per origin/version/range.
Unmodified regions keep their earlier origins. Never label an entire image with
the newest draw and thereby attribute all prior pixels to that draw. CPU uploads,
clears, blits, resolves, or uncaptured earlier frames require additional event
adapters or explicit missing-producer status; draw-only input cannot invent them.
Allocation IDs/versions are supplied by the future capture service, not by a
content hash, current RAM contents, or a presumed last writer.

`TraceResourceInputs` follows captured edges upstream. `TraceResourceInfluence`
follows them downstream. `TraceShaderObjectUsage` exposes those draw lists
separately from `object_candidates`, suggestions, and shared-resource edges.
The graph's missing-read diagnostics cover the supplied capture collection.
These lists mean captured resource-level reachability, not exact pixel history.

## Example: separate the character from everything it affects

```text
D18: character body, object 7, selected shader
D19: armor, object 7, different shader       -> confirmed part, not shader seed
D20: another character, object 8            -> separate instance
D23: character shadow pass, object 7        -> confirmed pass
D40: ground reads a produced shadow version -> downstream receiver, not body part
D80: reflection/composite reads scene color -> downstream effect, not model mesh
```

A selected receiving draw can also show the shadow producer in its upstream
inputs. Sharing a texture between D18 and D20 creates resource-sharing evidence,
not membership and not a read-after-write dependency. One batched draw with two
confirmed segment IDs produces two object candidates, not one model.

For a final composite, resource tracing may reach many scene draws. That is a
useful dependency view, but identifying exactly which object contributed a
particular reflected or shadowed pixel requires coverage/pixel-history work.

## How the renderer capture should supply actual geometry and materials

Use the final submission boundary, not a later RAM scan. Preserve the guest draw
and each backend emission when one guest submission is split/expanded. Assign
stable event IDs before caches, suppression, or batching can obscure provenance.

Resolve each vertex stream using its DMA selection/base, offset, element range,
stride, format, and component count. Copy referenced bytes and inline values;
retain exact indices, primitive order, restart/degenerate boundaries, and original
submission type. Existing OpenGL vertex fetch-range resolution is an integration
anchor; use the equivalent Vulkan ownership boundary, not UI access to live state.
Do not assume input v0 is position or an arbitrary constant block is a world
matrix. Original vertex programs/constants can replay the captured pose; a free
camera/attribute viewer must explicitly declare its interpretation.

For every material binding, retain the active stage, texture shape/subresources,
format, palette, sampler settings, constants, combiner/fixed-function state,
vertex partner, raster/blend/depth/stencil state, and needed destination contents.
The original engine material name is optional provenance, not a capture prerequisite.
Keep raw guest bytes/interpretation separate from the host resource actually
sampled: regenerating a stale or wrongly decoded host texture could hide the bug.

Capture-next-match and bounded frame capture must be explicit diagnostic actions.
Metadata is not a time machine: selecting an old draw can recover its bytes only
if a snapshot was retained. Otherwise capture a new occurrence and show its ID.
A capture pause/readback may have a measured cost; ordinary gameplay and opening
the preview must not wait for it. Publish owned immutable snapshots with declared
byte limits, cancellation, generation checks, and no UI access to mutable handles.
The current one-shot control owns a bounded position/index snapshot and cancels
on selection change or when the workbench is hidden. Full material ownership,
readback, and frame capture remain future work.

## Current limits and unfinished integration

The offline analyzers reject more than 256 draws, 512 effective segments, or 32
resource touches per draw. Selected primitive lists total at most 262,144 entries.
Triangle segmentation accepts at most 786,432 indices. The dependency edge limit
is 32,768. Duplicate draw/segment keys and inconsistent parent keys are rejected.
Limit or invalid-input flags must be shown as incomplete analysis, never success.
These are bounded foundation windows, **not** a full-frame capacity guarantee or
an immutable snapshot byte budget. Object relationship analysis is quadratic and
must stay off the rendering thread; indexed/paged analysis is future work.

Still required: backing-version tracking and all non-draw producer events,
additional NV2A formats/topologies, OpenGL/Vulkan resource copies, original-stage
replay, Material/Related Draws panels, user membership editing, persistence,
and native game validation.
`CaptureCompleteness` is descriptive metadata; it does not validate replay payloads.
No full model extraction, complete replay, pixel causation, or speedup is claimed.

## Verification

From an existing configured xemu build:

```sh
meson test -C build test-xemu-shader-browser-draw-capture --print-errorlogs
```

The same production sources can be tested without graphics dependencies:

```sh
c++ -std=c++17 -pthread -I. -Wall -Wextra -Werror -O2 \
  tests/unit/test-xemu-shader-browser-draw-capture.cc \
  ui/xui/shader-browser-draw-capture.cc \
  ui/xui/shader-browser-draw-request.cc \
  ui/xui/shader-browser-draw-segmentation.cc \
  ui/xui/shader-browser-object-relationship.cc \
  ui/xui/shader-browser-object-grouping.cc \
  ui/xui/shader-browser-resource-dependencies.cc \
  -o /tmp/xemu-capture-test
/tmp/xemu-capture-test
```

Tests use explicit checks that remain active under `NDEBUG`. They exercise
25 cases including atomic request matching, deterministic cancel/re-arm,
owned resolved generations, independent draw-array ranges, unrelated objects,
confirmed multi-pass membership, separate
instances, noncontiguous islands, ambiguous batches, screen-space effects,
aliasing, version changes, upstream/downstream traversal, provenance gaps,
invalid metadata, and admission limits. Unit results are not native capture or
replay evidence. Exact commands/results and verification limits belong in the PR.
