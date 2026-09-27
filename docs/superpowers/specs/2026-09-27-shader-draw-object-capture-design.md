# Shader Draw and Object Capture Design

## Status

Architectural foundation for a draft PR stacked on
`feature/shader-browser-stage4`. This design extends the synthetic shader
workbench toward explicit draw capture without weakening the existing rule that
normal gameplay must not wait for preview work.

## Purpose

The Shader Browser must be able to start from a selected shader and answer four
different questions without collapsing them into one misleading identity:

1. Which exact draw events used this shader?
2. Which geometry, textures, constants, and destinations did each draw touch?
3. Did one draw contain one object or several separable geometry segments?
4. Which neighboring draws are likely additional passes or parts of the same
   visible object?

The GPU boundary normally does not retain an engine-level model or material
name. The exact unit available to xemu is the draw event. Model reconstruction
therefore proceeds from draw evidence rather than from a guessed asset pointer.

## Identity layers

```text
ShaderKey
  -> DrawEventKey(session epoch, renderer epoch, frame, draw)
    -> DrawSegmentKey(draw, segment)
      -> ObjectCandidate(confirmed/suggested segment set)
```

A shader can seed many unrelated draws. A draw is exact. A draw segment is exact
only to the degree that the capture stage can prove its boundary. An object
candidate is always an inference unless the user confirms it or title-specific
knowledge proves it.

Guest addresses are not durable identities. Every resource reference carries
its draw identity, descriptor identity, content identity, guest range, and
resource kind. Equal bytes copied to another address are content-equivalent but
are not silently treated as the same allocation.

## Draw resource graph

Each draw publishes typed resource edges rather than a flat "material" pointer:

- vertex and index streams;
- textures and palettes;
- constant blocks and transform/skinning fingerprints;
- color and depth/stencil destinations;
- shader stage identities;
- read, write, read/write, bind-only, or derived-from access.

The effective material for a draw is the shader set plus appearance-related
resource edges and render state. The resource graph remains separate from object
grouping. Two objects sharing a texture, render target, shader, or vertex buffer
must not be merged solely for that reason.

## Splitting one draw

A single draw may represent:

- one ordinary submesh;
- several disconnected index components;
- several instances or transform clusters;
- geometry separated by degenerate strip primitives;
- a manually batched collection of unrelated objects.

Segment zero is the whole-draw fallback. Stronger capture stages may emit more
segments with provenance such as connected index component, degenerate
separator, instance, transform cluster, or user-defined split.

The first implementation includes indexed triangle-list connected-component
segmentation. Degenerate triangles do not join components. This is a hint, not
proof: one object may contain disconnected parts and a batching system may
share vertices between logical objects.

## Object relationship evidence

Relationship analysis is pairwise and inspectable. Positive evidence includes:

- identical geometry and transform/skinning fingerprints;
- shared geometry allocation with the same transform;
- overlapping captured vertex ranges;
- matching transform or skinning fingerprints;
- overlapping bounds;
- adjacent draws in the same frame.

Negative or limiting evidence includes:

- different non-empty transform fingerprints;
- different session, renderer epoch, or frame;
- resource sharing without geometry/transform agreement;
- same shader without any object evidence.

Only an exact geometry plus transform/skinning match is automatically grouped as
another pass over the same object. Shared buffers and transforms are exposed as
suggestions because large scenes and batching commonly reuse both. Different
transforms block object grouping and may still produce a resource-only edge.

## Shader object trace

Starting from a selected shader, the trace contains:

- every exact seed draw that used the shader;
- automatically grouped same-object passes containing those draws;
- related draw/segment suggestions for attached parts or candidate object
  membership;
- resource-only edges for shared textures, palettes, vertex/index streams, and
  other dependencies.

This allows one shader to lead to several independent object candidates and one
object candidate to include draws using different shaders.

## Capture progression

The feature advances through explicit completeness levels:

1. `MetadataOnly` — identities, counts, and lightweight references.
2. `ReferencedResources` — resolved guest ranges and descriptors.
3. `OwnedSnapshot` — immutable copied geometry, textures, constants, and state.
4. `ApproximateReplay` — declared substitutions remain.
5. `CompleteReplay` — every required dependency is represented.

Opening the Shader Browser must not continuously copy game memory. A later
capture service will arm "next matching draw" or bounded one-frame capture,
copy immutable data at the draw boundary, and publish it outside PGRAPH. Large
copies, decoding, grouping, and UI work remain off the ordinary draw path.

## Initial PR scope

This PR establishes the reusable, renderer-independent foundation:

- exact draw and segment identities;
- typed resource references and strict resource identity;
- shader-to-draw lookup;
- whole-draw fallback and triangle-list component segmentation;
- conservative object relationship classification;
- automatic same-pass grouping, suggestions, and resource-only edges;
- shader-seeded object tracing;
- focused unit tests and developer documentation.

It does not yet copy live PGRAPH resources, render captured geometry, expose the
workflow in XUI, or claim complete replay. Those steps depend on backend capture
adapters and the Stage 4 preview packet boundary.

## Integration sequence

1. Add a bounded metadata ring beside the existing shader draw observation path.
2. Assign `DrawEventKey` at the final draw-submission boundary for OpenGL and
   Vulkan.
3. Publish lightweight vertex/index/texture references for recent matching
   draws.
4. Add explicit "capture next matching draw" and one-frame capture requests.
5. Copy immutable resources with per-capture byte limits and deduplication.
6. Decode vertex streams and expand supported topologies into draw segments.
7. Feed owned snapshots into Stage 4 replay packets.
8. Add Geometry, Material, Related Draws, and Resource Graph views.

## Safety and performance constraints

- No ordinary draw may allocate large buffers, hash full resources, perform file
  I/O, wait for preview work, or block on the UI.
- Metadata rings and capture requests are bounded.
- Resource copies occur only for explicit capture and are immutable once
  published.
- Address reuse cannot alias captures because event and content identities are
  retained.
- Unsupported topology, format, or dependency is reported rather than silently
  replaced.
- Object inference remains visible and reversible; weak evidence never becomes
  an engine-level fact.
