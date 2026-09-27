# Shader Draw and Object Capture Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans or
> superpowers:subagent-driven-development for the remaining implementation.

**Goal:** Seed geometry/material inspection from a draw using a selected shader,
without confusing object membership with shared resources or visual influence.

**Architecture:** Renderer-independent, owned-record analysis. Exact draws and
primitive selections feed conservative object candidates. Confirmed membership
and versioned producer/consumer traversal are independent.

**Tech Stack:** C++17, existing Shader Browser types, Meson focused unit executable.

**Spec:** `docs/superpowers/specs/2026-09-27-shader-draw-object-capture-design.md`

## Global constraints

Stack on #241 / `feature/shader-browser-stage4`. Do not touch normal PGRAPH draws
in this foundation. Do not infer ownership from hashes, textures, or buffer
addresses. Preserve unknown inputs and scope/epoch/frame boundaries. Report
analysis limits and missing dependencies. No native replay claims from unit tests.

## Review focus

Address reuse and aliases; partial/ambiguous resource provenance; same mesh in
separate instances; multiple objects in one draw; fullscreen downstream effects;
wrong coordinate spaces; cross-title/session/frame grouping; disabled assertions.

## Foundation delivered

- [x] `shader-browser-draw-capture.hh/.cc`: exact keys, reference model, admission.
- [x] `shader-browser-draw-segmentation.cc`: triangle-list islands/primitive IDs.
- [x] `shader-browser-object-relationship.cc`: inspectable evidence, no inferred ownership.
- [x] `shader-browser-object-grouping.cc`: explicit membership and unresolved geometry.
- [x] `shader-browser-resource-dependencies.cc`: backing/version/range joins and
      upstream/downstream traversal, with missing/ambiguous provenance reporting.
- [x] `test-xemu-shader-browser-draw-capture.cc`: standalone production-source tests,
      including a regression for automatic cross-session grouping.
- [x] `ui/xui/meson.build`: register the same production sources and focused test;
      remove dependence on embedding implementation files in another test.
- [x] Developer README and corrected design contract.

## Remaining stages: not implemented by this draft

### 1. Capture producer and control surface

Add explicit next-match/frame requests beside shader observations and final
OpenGL/Vulkan submission boundaries. Preserve guest-to-host emission provenance.
Test cancellation, title/reset/renderer epochs, missing retained historical data,
request limits, and no work when unarmed. Use actual owner-thread state, not the
CPU's current PC as a guess at asynchronous GPU submission provenance.

### 2. Immutable resource storage and lineage

Resolve vertex DMA/offset/stride/formats, indices/inline values, texture
subresources/palettes/samplers, constants and destination state. Capture raw guest
and resolved host values separately. Allocate backing incarnations and read/write
versions, including non-draw producers and partial-write provenance fragments.
Test reused addresses, stale host textures, format-changing aliases, readback
synchronization, partial uploads, depth/color aliases, and byte-budget rejection.

### 3. Geometry and material inspection

Decode actual NV2A attribute formats and supported topologies; preserve original
primitive selections and stage inputs. Do not assume v0 position or a constant
range's matrix meaning. Publish material snapshots and interpretable mesh views.
Test inline/ranged/indexed/degenerate cases, multiple disconnected parts in one
object, and several batched objects in a single connected submission.

### 4. Preview, confirmation, and object/data-flow views

Adapt owned captures to Stage 4 replay packets and add Geometry, Material,
Object Membership, Inputs, and Downstream views. User-selected segments can
receive confirmed capture-local IDs. Show camera/input substitutions and
incomplete data explicitly. Add indexed/paged analysis before whole-frame scale.

### 5. Native evidence before ready-for-review

Run full builds and focused tests on supported platforms. Validate OpenGL and
Vulkan with fixed build/title/settings/capture identities. Compare original
captured output, repeat replay, and edited shader output; keep synthetic tests
separate. Measure unarmed, metadata-only, capture, and replay overhead. Exercise
stale texture/readback bugs, skinned poses, shadow receivers, reflected objects,
and fullscreen composites. Run clang-format and review source diffs. No merge
approval until these native stages and required reviews have evidence.
