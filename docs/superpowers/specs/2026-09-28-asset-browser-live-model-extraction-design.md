# Asset Browser: Live Model, Mesh, Texture, and Material Extraction

**Status:** Draft design for implementation review  
**Date:** 2026-09-28  
**Branch:** `feature/asset-browser-live-extraction`  
**Stacked on:** `feature/shader-draw-object-capture` / draft PR #259 at `06334cb5020c60b2b26a8304a348cce882ae82f0`

## Summary

Add a separate **Debug → Asset Browser** workspace that captures renderer-facing draw inputs from live xemu execution, reconstructs reusable mesh/material parts, lets the user assemble a complete model instance across multiple draws and shaders, and exports the result as GLB plus a provenance package.

The Asset Browser is not another synthetic shader fixture. It starts from actual draw occurrences and retains the geometry, every active vertex attribute, indices/topology, textures, palettes, samplers, constants, companion shader stages, transforms, and pipeline state needed to understand or replay that occurrence.

The Asset Browser and Shader Browser share capture identities and immutable draw snapshots. A selected asset part can be sent to Shader Browser with its real captured inputs, and a selected shader occurrence can be opened in Asset Browser to locate its owning geometry/material context.

The first complete export target is **GLB 2.0**. GLB is open, portable, supports scenes, multiple mesh primitives/materials, textures, nodes, skins, animations, and custom `extras`, and can be validated independently. FBX remains an additional writer behind the same format-independent scene model rather than a dependency of the capture architecture.

## Why this must be a separate subsystem

The current PR #259 capture is deliberately preview-sized. `OwnedDrawGeometry` contains only positions and indices, and the copy helper accepts only float3/float4 positions with a 4096-vertex and 12288-index ceiling:

- <https://github.com/Mainkill1/xemu/blob/06334cb5020c60b2b26a8304a348cce882ae82f0/ui/xui/shader-browser-draw-request.hh>
- <https://github.com/Mainkill1/xemu/blob/06334cb5020c60b2b26a8304a348cce882ae82f0/hw/xbox/nv2a/pgraph/shader-browser-geometry-copy.h>

That is useful for an approximate shader preview, but it cannot produce a complete external model because it does not retain UVs, normals, tangents, colors, skin weights, joint indices, exact texture images, or complete material state.

The existing draw-capture foundation is still valuable. It already separates shader identity, exact draw identity, draw segments, object candidates, and versioned resource dependencies. Asset Browser should reuse those concepts rather than infer that one shader equals one object:

- <https://github.com/Mainkill1/xemu/blob/06334cb5020c60b2b26a8304a348cce882ae82f0/ui/xui/shader-browser-draw-capture.hh>

A separate menu and service keep export-sized resource ownership, decoding, assembly, and file writing out of the Shader Browser preview lifecycle.

## Research references and lessons

### RenderDoc

RenderDoc uses an event-centered workflow: an event/action is selected first, then mesh, textures, pipeline state, and shader interfaces are inspected in synchronized views. Its Mesh Viewer exposes vertex input and post-stage output rather than treating a shader as a complete model.

- Event Browser: <https://github.com/baldurk/renderdoc/blob/v1.x/docs/window/event_browser.rst>
- Mesh Viewer quick start: <https://github.com/baldurk/renderdoc/blob/v1.x/docs/getting_started/quick_start.rst>
- Shader Viewer: <https://github.com/baldurk/renderdoc/blob/v1.x/docs/window/shader_viewer.rst>

**Applied lesson:** Asset Browser must select exact draw occurrences and synchronize all views to that event. It must expose both source vertex data and evaluated stage output where supported.

### Xenia GPU Trace Viewer

Xenia contains an emulator-specific GPU trace player/viewer with frame and command navigation, packet disassembly, shader representations, texture inspection, and replay through the emulator GPU implementation.

- <https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/gpu/trace_viewer.cc>

**Applied lesson:** Preserve guest command/state provenance and emulator-specific interpretations, not only host API objects. A saved capture should remain inspectable after the original game state is gone.

### GFXReconstruct, apitrace, and GITS

These projects record graphics command streams for later replay. GFXReconstruct also supports targeted dumping of vertex/index buffers, render targets, and descriptor-bound resources during replay.

- GFXReconstruct: <https://github.com/LunarG/gfxreconstruct>
- Resource dumping: <https://github.com/LunarG/gfxreconstruct/blob/dev/vulkan_dump_resources.md>
- apitrace: <https://github.com/apitrace/apitrace>
- GITS: <https://github.com/intel/gits>

**Applied lesson:** Treat resource versions and ordered events as first-class data. Asset extraction should be performed from owned immutable snapshots or deterministic replay, not from later mutable addresses.

### Practical extraction tools

Ninja Ripper describes extracting all available vertex attributes, indices, textures, and shaders. MapsModelsImporter demonstrates assembling models from RenderDoc captures into Blender. RDHelper demonstrates event-range mesh/texture discovery, but its OBJ export explicitly lacks UVs and vertex colors, illustrating why position-only export is insufficient.

- Ninja Ripper source: <https://github.com/riccochicco/ninjaripper>
- MapsModelsImporter: <https://github.com/eliemichel/MapsModelsImporter>
- RDHelper: <https://github.com/mifth/RDHelper>

**Applied lesson:** Capture every attribute and material binding first. Export is a downstream serialization problem; missing source attributes cannot be recreated reliably by the writer.

### GLB/glTF and writer

The glTF 2.0 model maps well to draw-oriented capture: a mesh contains primitives, each primitive binds attributes, indices, topology, and a material; nodes instantiate meshes with transforms; skins and animations are optional extensions of the same scene.

- glTF 2.0 specification: <https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html>
- `cgltf` loader/writer: <https://github.com/jkuhlmann/cgltf>
- Khronos validator: <https://github.com/KhronosGroup/glTF-Validator>

**Decision:** Vendor the dependency-free MIT-licensed `cgltf` writer through the existing Meson subproject/vendor mechanism, or implement the same writer interface internally if dependency review rejects vendoring. Asset Browser code depends on an `AssetSceneWriter` interface, not directly on `cgltf` types.

## Goals

1. Open an independent **Asset Browser** from the Debug menu.
2. Start from a selected live draw, shader occurrence, captured frame/window, or imported capture package.
3. Retain complete renderer-facing vertex/index inputs rather than position-only geometry.
4. Retain exact resource versions for textures, palettes, constants, and other material inputs.
5. Decode supported NV2A formats into a format-independent mesh/material representation while preserving raw bytes and provenance.
6. Assemble one complete model instance from parts spread across several draws, materials, and shaders.
7. Keep asset, instance, pose, draw pass, and downstream dependency identities separate.
8. Allow user-confirmed assembly whenever automatic ownership evidence is ambiguous.
9. Export a useful GLB plus a capture/provenance package and validation report.
10. Feed selected asset/draw context back into Shader Browser for realistic shader replay and replacement testing.
11. Add no large copies, readbacks, file I/O, or decoding work to ordinary gameplay while capture is disabled.
12. Support both OpenGL and Vulkan through one renderer-neutral snapshot contract.

## Non-goals for the first implementation

- Universal recovery of original game filenames, engine class names, or object hierarchies.
- Claiming that equal shaders, textures, addresses, or hashes prove object ownership.
- A continuous unbounded full-session GPU trace.
- Exact reconstruction of a rig or animation when the game never exposes the required bind/hierarchy data to the captured draw.
- Full RenderDoc-style pixel history or per-invocation shader debugging.
- Making FBX SDK integration a prerequisite for useful extraction.
- Hiding unsupported or missing data by silently fabricating normals, UVs, textures, or transforms.

## User workflow

### Entry points

- **Debug → Asset Browser** opens the independent workspace.
- Shader Browser gains **Open occurrence in Asset Browser**.
- Asset Browser gains **Inspect shader in Shader Browser**.
- A captured draw package can be reopened without running the original encounter.

### Capture workflow

1. Select a draw/shader occurrence or arm a bounded capture.
2. Choose a goal:
   - Next draw with exportable geometry.
   - All matching occurrences in the current frame.
   - Bounded before/after animation window.
   - Full selected frame action/resource capture.
3. Continue over unsupported matches when the goal requires usable geometry, while retaining rejection evidence.
4. Pause only when the requested capture goal is satisfied or the user cancels.
5. Decode snapshots off the renderer thread.
6. Inspect parts and confirm or correct model membership.
7. Export the selected pose/model or retain it as a shader replay fixture.

### Model assembly workflow

```text
Selected draw or shader occurrence
    → exact captured mesh/material part
    → suggested related parts from the same instance/frame/window
    → user-confirmed model assembly
    → optional title-specific memory resolver adds hidden/unsubmitted parts
    → GLB + provenance package
```

Automatic suggestions may use geometry resource identity, transforms, skinning palette evidence, spatial bounds, submission context, and repeated-frame correspondence. No individual heuristic establishes ownership.

## UI layout

The Asset Browser follows the broad Shader Browser pattern but is optimized for occurrences and assemblies rather than shader identities.

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ Asset Browser                                                                │
│ [Capture next] [Capture frame] [Arm window] [Stop] [Save capture]            │
├────────────────────┬───────────────────────────────────┬─────────────────────┤
│ DRAWS / PARTS      │ ASSEMBLED MODEL VIEW              │ INSPECTOR           │
│                    │                                   │                     │
│ Search/filter      │ Camera-adjustable 3D viewport     │ Geometry            │
│ Frame 4259         │ with selected part highlighted    │ Materials           │
│  D412 Body?        │                                   │ Textures            │
│  D438 Head?        │ [Captured pose] [Raw inputs]      │ Shaders             │
│  D451 Eyes?        │ [Wireframe] [UV] [Normals]        │ Transforms          │
│                    │                                   │ Provenance          │
├────────────────────┴───────────────────────────────────┴─────────────────────┤
│ ASSEMBLY / EXPORT                                                           │
│ Included parts | Suggested parts | Excluded passes | Missing dependencies   │
│ [Add] [Remove] [Confirm model] [Send to Shader Browser] [Export GLB]         │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Interaction requirements

- Up/down arrows change the selected visible occurrence and immediately update all synchronized views.
- Left/right arrows expand/collapse hierarchy or move among grouped parts.
- Large selectors support type-to-filter, Home/End, Page Up/Down, immediate preview, Enter to keep, and Escape to restore the original value.
- The viewport remains large by default. Inspector and export detail panes are collapsible.
- The UI always shows whether a value is **Captured**, **Decoded**, **Derived**, **User-assigned**, **Substituted**, **Missing**, or **Unsupported**.
- User labels such as “enemy body” remain capture-local unless a title adapter establishes persistent asset identity.

## Architecture

### 1. `AssetBrowserWindow`

Independent ImGui window and state under `xemu::asset_browser`. It owns only presentation state and selected IDs. It never reads mutable renderer handles.

Proposed files:

- `ui/xui/asset-browser.hh`
- `ui/xui/asset-browser.cc`
- `ui/xui/asset-browser-view.inc`
- `ui/xui/asset-browser-inspector.inc`
- `ui/xui/asset-browser-assembly-view.inc`

Menu integration:

- `ui/xui/menubar.cc`
- `ui/xui/xemu-hud.h` and the external-window controller only if Asset Browser is given a separate native window in the first implementation.
- `ui/xui/meson.build`

Initial delivery may use the ordinary xemu debug window path while keeping the window class independent. External native-window support should reuse the Shader Browser mechanism rather than duplicate SDL/ImGui cursor ownership.

### 2. `AssetCaptureService`

Owns capture requests, byte/resource budgets, cancellation, generation checks, immutable completion, and the current capture repository.

Proposed files:

- `ui/xui/asset-browser-capture.hh`
- `ui/xui/asset-browser-capture.cc`
- `ui/xui/asset-browser-capture-store.hh`
- `ui/xui/asset-browser-capture-store.cc`

The fast path is a single atomic/epoch gate when no request is armed. Large copies occur only for explicit bounded requests.

### 3. Renderer-neutral snapshot bridge

PGRAPH establishes exact guest draw and state identity. OpenGL and Vulkan adapters provide the final resolved resources used by the emitted command.

Proposed files:

- `hw/xbox/nv2a/pgraph/asset-capture-bridge.h`
- `hw/xbox/nv2a/pgraph/asset-capture-bridge.c`
- `hw/xbox/nv2a/pgraph/gl/asset-capture-gl.c`
- `hw/xbox/nv2a/pgraph/vk/asset-capture-vk.cc`

The bridge must preserve one guest draw to many backend emissions. A suppressed or failed emission must not be published as a successful rendered occurrence.

### 4. Decoders

Pure, renderer-independent decoders convert owned raw inputs into reusable asset records.

Proposed files:

- `ui/xui/asset-browser-vertex-decode.hh/.cc`
- `ui/xui/asset-browser-index-decode.hh/.cc`
- `ui/xui/asset-browser-texture-decode.hh/.cc`
- `ui/xui/asset-browser-transform-analysis.hh/.cc`

These units must be testable without OpenGL/Vulkan contexts.

### 5. Assembly service

Builds suggestions and confirmed assemblies from decoded parts while preserving draw/pass/resource relationships.

Proposed files:

- `ui/xui/asset-browser-assembly.hh/.cc`
- `ui/xui/asset-browser-memory-resolver.hh/.cc`

The default resolver uses generic renderer evidence. Optional title adapters are registered by title ID and executable fingerprint and can resolve engine model/skeleton structures from guest memory.

### 6. Format-independent extracted scene

All exporters consume one representation:

```text
ExtractedScene
  Nodes / transforms / instances
  Meshes
    Primitives
      Attributes
      Indices / topology
      Material reference
      Capture provenance
  Materials / passes
  Images / samplers / textures
  Skins / joints / inverse-bind data
  Animation or evaluated-pose samples
  Warnings and unresolved fields
```

Proposed files:

- `ui/xui/asset-browser-scene.hh/.cc`
- `ui/xui/asset-browser-export.hh/.cc`
- `ui/xui/asset-browser-gltf.cc`

### 7. Shader Browser bridge

A shared selection context contains `ShaderScope`, `DrawEventKey`, stage identities, resource snapshot IDs, and an optional assembly part ID.

Proposed files:

- `ui/xui/asset-shader-selection.hh/.cc`

Neither window calls the other window’s rendering code. Both observe/update the shared selection service.

## Capture data model

### Shared immutable blob storage

Large raw buffers and images are deduplicated by capture-local resource identity/version/range and retained through reference-counted immutable blobs.

```text
AssetBlobId
AssetResourceVersionKey
  storage_id
  version
  canonical byte range
  descriptor digest
  content digest
```

Equal content at unrelated addresses is not the same resource. Address reuse after another allocation creates another storage incarnation.

### Draw snapshot

```text
AssetDrawSnapshot
  DrawCaptureSummary identity/relationships
  vertex attributes and owned source blobs
  index input and original topology/subdraw ranges
  active guest and generated host shader stages
  vertex/pixel/fixed-function constant banks
  texture, palette and sampler snapshots
  blend/depth/stencil/raster state
  destination identity and required prior contents
  transform/coordinate-space interpretations
  completeness and rejection diagnostics
```

### Vertex input

For every NV2A attribute slot, retain:

- Enabled, streamed, inline, or constant source mode.
- DMA selection, resolved address/range, offset, stride, format, component count, normalization, signedness, and channel order.
- Raw bytes for referenced elements.
- Exact decoded values supplied to the guest/host shader.
- Source generation/version.
- Semantic interpretation separately from slot identity.

The decoder must support the active formats already interpreted by xemu, including normalized bytes/shorts, float, short-as-integer, packed signed 11/11/10, compressed inputs, and BGRA swizzling. Unsupported formats remain export-visible raw data with a precise status.

### Index/topology input

Retain original index width/bytes, draw arrays and subdraw ranges, base offsets, primitive mode, strip/fan ordering, degenerates, and guest-to-host expansion mapping.

Export may triangulate supported topologies, but it must preserve an original-primitive map. Vertices must not be welded solely by equal positions because UV, normal, color, or skin seams may differ.

### Texture/material input

For each active stage retain:

- Resource identity/version and exact owned data.
- Dimensions, dimensionality, pitch/layout, format, mip count/range, cubemap faces or volume slices.
- Palette bytes and interpretation.
- Raw guest representation and, when different, the host-decoded/sampled representation.
- Filter, wrap, border, LOD and texture-coordinate generation/transform state.
- Shader stage and coordinate source that consumes the texture.

Reuse existing texture shape/address/length logic rather than independently reimplementing NV2A layout rules:

- <https://github.com/Mainkill1/xemu/blob/06334cb5020c60b2b26a8304a348cce882ae82f0/hw/xbox/nv2a/pgraph/texture.c>

### Geometry representations

A snapshot may expose several representations:

1. Raw vertex inputs.
2. Decoded pre-deformation mesh inputs.
3. Evaluated vertex-stage output for the captured pose.
4. Export-space geometry after an explicitly recorded coordinate conversion.

Final clip-space `gl_Position` is not automatically a model-space mesh. Coordinate-space provenance is mandatory.

### Skeleton and animation

The generic renderer path may recover weights, palette indices, and captured matrices. Original hierarchy and bind data require either an established shader/memory interpretation or a title-specific resolver.

Export modes are explicit:

- Current evaluated pose.
- Rest mesh + recovered/derived rig.
- Captured animation range.
- Evaluated vertex cache when a stable rig cannot be recovered.

A reconstructed rig must not be labeled as the original engine skeleton.

## Renderer integration requirements

### OpenGL

At the final draw boundary, capture the actual bound/resolved vertex and index sources, all enabled attribute descriptions, active programs/stages, uploaded uniforms, textures/samplers, and state. Owned data must be copied before the source can be mutated or reused.

Readbacks must be explicit and bounded. Capture must restore any temporary GL bindings/state it changes.

### Vulkan

Capture resolved draw descriptors, private/transient vertex generations, index data, descriptor-bound images/buffers, push constants/UBOs, stages, and pipeline state from the same generation used by `vkCmdDraw*`.

GPU image copies and host-visible completion are asynchronous. The UI receives only completed owned snapshots, never live Vulkan handles.

### Common PGRAPH provenance

Retain raw PGRAPH/guest descriptors and decoded values alongside host-applied values. This lets Shader Browser distinguish a bad game input, bad xemu decode, stale host resource, or bad generated shader.

## Model assembly rules

Three identities remain separate:

- **Mesh asset:** reusable geometry/material parts.
- **Model instance:** one placement/character/object using those parts.
- **Captured pose:** that instance at one frame/time.

A draw is not automatically one object. One draw can batch objects; one object can use many draws, materials, shaders, and passes.

Suggested membership may use:

- Explicit engine/title-adapter ownership.
- Equal captured instance/skin palette identity.
- Shared transform evidence.
- Repeated geometry with a consistent instance transform.
- Spatial bounds and frame-local submission context.
- User confirmation.

Shared shader or texture identity is evidence only and never automatic membership.

Rendering passes that reuse the same physical surface remain pass records rather than duplicate mesh parts. Downstream shadow receivers/composites remain dependencies, not model members.

## Export behavior

### GLB

The first writer exports:

- Scene nodes and transforms.
- Mesh primitives split by material/pass assignment where needed.
- POSITION and all confidently interpreted NORMAL, TANGENT, TEXCOORD_n, COLOR_n, JOINTS_n, and WEIGHTS_n attributes.
- Original or triangulated indices with provenance.
- Portable materials and embedded images where representable.
- Skins/animations when validated.
- `extras` containing capture IDs and unresolved-field summaries.

The writer also emits `export-report.json` and runs the Khronos validator when available in the developer/test environment.

### Provenance package

Every export has a companion directory or bundle containing:

```text
capture-manifest.json
raw/vertex-streams/
raw/index-streams/
raw/textures-and-palettes/
decoded/textures/
shaders/generated-and-guest/
state/constants-and-pipeline/
assembly/membership.json
export-report.json
```

The portable GLB may approximate complex Xbox material behavior. The provenance package retains the faithful renderer-facing state needed by Shader Browser and future replay.

### FBX

`AssetSceneWriter` permits a later FBX writer without changing capture or assembly. The initial implementation does not add Autodesk FBX SDK as a required dependency.

## Shader Browser backfeed

When a part or draw is selected, **Inspect shader in Shader Browser** publishes:

- Exact draw/shader identities.
- Captured geometry representation.
- Captured textures/samplers/constants.
- Companion stages.
- Render state and replay-completeness status.

Shader Browser then uses this data as the default preview fixture. Synthetic values move behind explicit advanced overrides.

When a replacement is tested, it can be replayed against:

- The selected occurrence.
- Every captured occurrence using that shader.
- The complete captured model assembly.
- A bounded dependency/frame replay once downstream propagation exists.

Saving or exporting an asset never activates a game override. Enabling a replacement remains an explicit Shader Browser action.

## Error handling and truthfulness

Every component has independent completeness state. Examples:

```text
Geometry        Captured and decoded
Normals         Missing
UV0             Captured and decoded
Texture T0      Captured guest bytes; host image unavailable
Skinning        Captured weights/palette; hierarchy unresolved
Material        Portable approximation; faithful state retained
Export          Completed with warnings
```

Unsupported data is retained when bounded and safe. The UI does not call an incomplete draw a complete model or a heuristic group a confirmed object.

Export should proceed with explicit warnings when a useful partial asset can be produced. It must fail when required structural data is invalid, such as no interpretable positions, out-of-range indices, or inconsistent buffer bounds.

## Performance and ownership

- Disabled path: one low-cost request/epoch check at renderer integration points.
- Explicit byte, draw, resource, and time budgets.
- Resource copies are deduplicated by version/range.
- Decode, assembly analysis, image conversion, and export run off rendering/PGRAPH threads.
- Cancellation and title/renderer/session generation changes invalidate pending work deterministically.
- No later access to guest pointers or mutable host handles after snapshot publication.
- UI lists and analysis are paged/indexed; do not run quadratic whole-session grouping every frame.

## Implementation sequence for the draft PR

The branch should become compile-ready in layers while remaining draft:

1. **Core model and tests**
   - Immutable blob store and snapshot types.
   - Vertex/index/texture descriptors.
   - Format-independent extracted scene.
   - Unit fixtures for shared shaders, multiple materials, dynamic resources, and sparse indices.
2. **Decoder and assembly core**
   - NV2A vertex format decoding.
   - Topology conversion with provenance.
   - Generic grouping suggestions and user-confirmed assembly.
3. **GLB writer**
   - Meshes, materials, images, nodes, warnings, and validator fixture.
4. **Asset Browser UI shell**
   - Menu entry, independent window, occurrence list, assembly viewport, inspector, export report.
   - Keyboard navigation and shared selection bridge.
5. **Capture contracts and no-op adapters**
   - Complete request/state/ownership API.
   - OpenGL/Vulkan adapter entry points compiled and returning precise missing/unsupported status until wired.
6. **Renderer wiring**
   - OpenGL all-attribute/resource snapshots.
   - Vulkan resolved-generation and asynchronous image snapshots.
7. **Native validation and Shader Browser integration**
   - Real game/test-XBE capture, reopen, export, and replacement replay.

The requested “mostly coded, then wired in” state corresponds to steps 1–5: core data structures, decoders, assembly, export, UI, tests, and renderer adapter contracts are implemented and compiling before invasive renderer capture hooks are added.

## Test requirements

### Unit tests

- All supported NV2A vertex formats and component counts.
- Interleaved and separate streams.
- Inline and constant attributes.
- 16/32-bit indices, arrays, strips, fans, degenerates, sparse referenced ranges, and more than 4096 vertices.
- UV/normal/color/skin seams preserved without position-only welding.
- Texture shape/layout/palette metadata and version deduplication.
- One shader used by at least 32 unrelated draw occurrences.
- One model assembled from parts using several shaders/materials.
- Repeated rendering passes do not duplicate physical mesh parts.
- GLB structural validation and deterministic output.
- Capture cancellation, generation invalidation, and budget exhaustion.

### Native OpenGL/Vulkan fixture

A test XBE should submit:

- A multi-part model with body/head/eyes using different shaders.
- Thirty-two instances sharing one shader and geometry with different transforms/constants.
- Multiple vertex streams with position, normal, color, UV, weights, and indices.
- Dynamic vertex updates and a texture overwritten between draws.
- Triangle lists, strips, and arrays.
- A mesh larger than the Shader Browser preview limit.
- A depth-only repeat pass and a downstream texture consumer.

The test captures, closes the live encounter, reopens the saved package, exports GLB, validates it, and compares counts/bounds/material assignments/resource digests against known expected data.

### Independent validation

- Khronos glTF Validator reports no errors.
- GLB opens in at least one independent viewer/importer.
- Mesh part counts, indices, bounds, UVs, texture contents, materials, and pose landmarks match the fixture.
- Disabled capture shows no measurable large-copy/readback/file-I/O behavior.

## Acceptance criteria

- [ ] Debug menu exposes an independent Asset Browser window.
- [ ] Selecting one shader reveals all exact draw occurrences; none are collapsed into one variable/material set.
- [ ] A captured draw can retain all active vertex attributes, exact topology, textures, palettes, samplers, constants, stages, transforms, and state within configured budgets.
- [ ] Asset capture is not limited by the Shader Browser’s 4096-vertex preview budget.
- [ ] Parts using different shaders can be assembled into one confirmed model instance.
- [ ] Shared shaders/textures do not automatically merge unrelated objects.
- [ ] User-confirmed assembly works without engine asset names.
- [ ] Optional title-specific resolvers can add original model/skeleton relationships without contaminating generic capture logic.
- [ ] GLB export includes supported geometry, materials, textures, transforms, skins/animation, warnings, and provenance.
- [ ] Exported files remain usable after xemu and the game are closed.
- [ ] Selected captured assets can populate Shader Browser with real geometry/material inputs.
- [ ] Renderer capture remains bounded, cancelable, generation-safe, and inert when disabled.
- [ ] OpenGL and Vulkan native evidence covers dynamic resources and exact resource-version selection.

## Design review decisions requested

1. Confirm **Asset Browser** as the user-facing name and **Debug → Asset Browser** as the menu location.
2. Confirm GLB as the first complete export format, with FBX behind a later writer.
3. Confirm this PR remains stacked on #259 so it can directly reuse draw/resource identity and be reviewed as a focused continuation.
4. Confirm that user-confirmed assembly is acceptable for generic games while title-specific memory resolvers are added incrementally.
