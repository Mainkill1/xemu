# Shader draw and object capture

## Concept

A shader does not own a model. The exact join point is a GPU draw event, where
xemu has the active shader set, geometry submission, textures, constants,
render state, and destinations at the same time.

This feature uses four separate identities:

```text
shader -> exact draw -> draw segment -> object candidate
```

That separation prevents two common mistakes:

- treating every draw that uses one shader as one object;
- treating every draw that shares a texture or vertex buffer as one object.

## Current foundation

The draft implementation adds a renderer-independent model for:

- exact draw identity across session, renderer epoch, frame, and draw number;
- multiple segments inside one draw;
- typed resource references with guest range, descriptor digest, content digest,
  slot, and access direction;
- exact shader-to-draw lookup;
- indexed triangle-list connected-component segmentation;
- conservative object relationship evidence;
- automatic grouping of exact same-object passes;
- separate attached-part suggestions and resource-only edges;
- tracing one shader to all seed draws and every candidate object it reaches.

## Object splitting rules

A draw with no segmentation evidence is represented by one whole-draw segment.
A capture decoder may split it into connected index components, instances,
degenerate-separated islands, transform clusters, or user-selected ranges.

Connected geometry is only a hint. A single object may have disconnected parts,
and a batched draw may contain several objects. The UI must display the split
provenance and permit correction.

## Relationship rules

Automatic grouping is intentionally narrow:

```text
same geometry + same transform or skinning = same-object pass
```

The following remain suggestions:

- shared vertex/index allocation plus matching transform;
- adjacent draws with matching transform and overlapping bounds;
- likely attached submeshes.

The following remain resource-only relationships:

- shared texture or palette;
- shared geometry allocation with different transforms;
- other shared dependencies that do not establish object identity.

Same shader has zero grouping weight. Different non-empty transform fingerprints
block object grouping.

## Example

```text
Selected shader P42
  Seed draw F120/D18 -> body segment
    Same-object pass: F120/D23 shadow/highlight pass
    Suggested part:   F120/D19 armor segment
    Resource-only:    F120/D70 environment using the same texture
  Seed draw F120/D91 -> unrelated vehicle segment
```

The trace therefore reports two object candidates, not one "model for P42".
It can still show every neighboring draw and resource the shader touched.

## What is not implemented yet

This foundation does not yet copy PGRAPH memory, decode all NV2A vertex formats,
capture OpenGL/Vulkan host resources, replay a captured draw, or add the XUI
Geometry/Material/Related Draws panels. It defines and tests the contract those
backend stages will publish into.

## Next integration steps

1. Add bounded draw metadata beside the current Shader Browser observation path.
2. Assign frame/draw IDs at the final backend submission boundary.
3. Resolve vertex/index/texture references without copying large data by default.
4. Add explicit capture-next-match and one-frame capture commands.
5. Copy immutable resources with byte limits and digest-based deduplication.
6. Decode geometry, publish draw segments, and send owned replay packets to the
   Stage 4 live preview.
