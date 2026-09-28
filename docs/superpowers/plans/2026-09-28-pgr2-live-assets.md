# PGR2 Live Asset Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Discover and inspect assembled captured cars during PGR2 gameplay.

**Architecture:** Reuse the current all-shader capture owner and immutable events.
A bounded CPU decoder/catalog publishes coherent model assemblies to an independent
Asset Browser window. One HUD-owned renderer draws the selected textured assembly
and caches visible thumbnails; live discovery and correspondence remain separate
from labeling, shader editing and renderer ownership.

**Tech Stack:** C++17, existing ImGui/OpenGL HUD, GLib, immutable capture blocks,
nlohmann JSON, existing NV2A vertex/topology evidence.

**Spec:** `docs/superpowers/specs/2026-09-28-pgr2-live-assets.md`, extending the existing
`2026-09-28-asset-browser-live-model-extraction-design.md` for this workflow.

## Global Constraints

- Remain on existing PR260, based on current PR259; preserve other worktrees.
- Own capture generations and data; no borrowed guest/GPU pointers after publication.
- Shared shader/texture/address alone is not object identity.
- No engine offsets or titles guessed from hashes; inferred/manual status is explicit.
- Capture is opt-in, bounded and inert when disabled; never stop a foreign recorder.
- Distinguish viewport FPS from capture freshness; target at least30FPS viewport.
- Display decoding/material approximation and original replay are distinct products.
- Restore HUD GL state and release resources before context destruction.

## Review Focus

- Identical opponents and changed draw order must not replace the pinned car arbitrarily.
- Reset/re-arm while an async catalog is being built must reject stale completions.
- Disabled/current attributes, sparse indices and independent strips must retain geometry bounds.
- Partial frame/budget/readback completion must not appear as a fresh complete assembly.
- Hidden/closed windows must release owned recording and GPU resources without stopping a foreign session.

### Task 1: Owned vertex decoding and model catalog

**Files:** Create `ui/xui/asset-browser-model.hh/.cc`,
`ui/xui/asset-browser-decode.hh/.cc`, `tests/unit/test-xemu-asset-browser-model.cc`;
modify `ui/xui/meson.build`.

**Interfaces:** Consumes `CaptureSessionSnapshot` and immutable `CaptureOccurrence`.
Produces `AssetCatalog BuildAssetCatalog(const CaptureSessionSnapshot &, const AssetLimits &)`;
`AssetDecodeResult DecodeAssetPart(std::shared_ptr<const CaptureOccurrence>, uint32_t backend, const AssetLimits &)`.
Catalog entries retain exact event IDs, shared part geometry/material evidence and frame.

- [ ] Write tests:32same-shader different draws, multipart grouping/manual membership,
  sparse referenced bounds, independent strip/subdraw boundaries, nonfinite/invalid
  inputs, OpenGL/Vulkan normalized/float formats, >4096vertices and budget status.
- [ ] Run strict test against declared rejecting interfaces; expected runtime assertions.
- [ ] Implement bounded decoding/catalog with captured UV/color and owned material images.
- [ ] Run strict and ASan/UBSan; expected all registered cases pass.
- [ ] Commit `xui: Decode owned captured asset parts` with evidence and AI declaration.

### Task 2: Live discovery, selection and assembly ownership

**Files:** Create `asset-browser-controller.hh/.cc`, `asset-browser-live.hh/.cc`,
`tests/unit/test-xemu-asset-browser-controller.cc`; modify capture session only if
the actual ownership API needs a narrowly scoped generation-safe extension.

**Interfaces:** Consumes Task1 catalog. Produces `AssetController` owning labels,
selected/pinned assembly, Live/Freeze, correspondence state and generation-safe
`Publish(AssetCatalog, uint64_t request_generation)`; `AssetLiveCapture` owns the
shared recorder incarnation, periodically requests complete bounded frames and
publishes CPU jobs. Callback/snapshot data never borrows mutable PGRAPH state.

- [ ] Write tests for unique/duplicate correspondence, reordered draws, immutable
  freeze, coherent multipart publication, missing/LOD/budget status, stale async
  completion, epoch reset and foreign-recording ownership; watch runtime RED.
- [ ] Implement controller and bounded asynchronous sampling with cancellation.
- [ ] Run strict/sanitized tests and existing capture/session tests; expected pass.
- [ ] Commit `xui: Own live asset discovery and assemblies`.

### Task 3: Textured assembly viewport and thumbnails

**Files:** Create `asset-browser-viewport.hh/.cc`,
`tests/unit/test-xemu-asset-browser-viewport.cc`, add native test target.

**Interfaces:** Consumes immutable selected `AssetAssembly` and camera; produces
`AssetViewport::Render` result texture and bounded thumbnail cache, with explicit
`Shutdown()` before HUD context destruction. All parts use one shared bound.

- [ ] Write native controlled multipart texture/UV/shared-bound and state-restoration
  tests, invalid texture budget and cache eviction; watch runtime RED.
- [ ] Implement cached VBO/index/texture/FBO drawing with orbit/pan/zoom and wireframe.
- [ ] Execute native GL fixture, strict/sanitized CPU checks; expected images match
  hand-defined samples and GL state is unchanged. Measure viewport cadence.
- [ ] Commit `xui: Render captured asset assemblies`.

### Task 4: Independent Asset Browser window and shader bridge

**Files:** Create `asset-browser.hh/.cc`; modify `menubar.cc`, `main.cc`,
`shader-browser.hh` and capture-workspace selection adapter, `meson.build`.

**Interfaces:** Consumes controller/live/viewport; exports global `asset_browser_window`
with `Draw()` and `Shutdown()`. Shader Browser gets an exact-owned occurrence entry
point, reusing existing recipe/source/replay setup without exposing private UI state.

- [ ] Add ImGui lifecycle fixture for keyboard live selection, filtering stable IDs,
  Live/Freeze, add/remove/label/pin, close and shared selection; watch runtime RED.
- [ ] Implement thumbnail explorer, large viewport, collapsible provenance/material
  inspector, live controls/freshness and Debug menu/HUD lifecycle integration.
- [ ] Compile every changed production TU with actual flags and run UI fixture.
- [ ] Commit `xui: Add live Asset Browser workspace`.

### Task 5: Owned persistence, GLB and stage inspection

**Files:** Create `asset-browser-export.hh/.cc` and export tests; modify viewport
and shader bridge only where needed for captured-stage/output inspection.

**Interfaces:** `SaveAssetAssembly`/`ReopenAssetAssembly` preserve immutable capture
and annotation identities; `ExportAssetGlb` emits geometry, supported materials,
textures, UVs and provenance. Stage inspection uses owned original sources/layout/
constants and reports precise unsupported state when evaluation cannot be established.

- [ ] Test saved32uses reopen/independent selection, malformed package, canceled
  output, deterministic GLB bounds/indices/UV/materials and independent import.
- [ ] Implement using existing capture package/export primitives and a small GLB writer.
- [ ] Validate export and stage fidelity/status on native fixtures; expected supported
  output correctness and explicit unsupported/missing limits.
- [ ] Commit `xui: Persist and extract captured asset assemblies`.

### Task 6: Review, native PGR2 validation and publication

**Files:** Evidence under `evidence/wiki-xiso-per-test/pr-260/live-assets/`;
update support documentation, checkpoint and existing PR260 description.

- [ ] Run full available unit suite, strict changed-TU build, sanitizers and formatting.
- [ ] Dispatch one fresh whole-feature reviewer, fix important findings with RED/GREEN.
- [ ] Native PGR2 Vulkan: find/assemble/pin car while playing, inspect textures and
  motion, freeze, send exact part to Shader Browser, save/reopen; OpenGL parity.
- [ ] Test duplicates, LOD, reset, budgets, close and measured viewport/gameplay overhead.
- [ ] Publish to existing branch and obtain current-head CI after verified local code.
  Keep draft for any unqualified native/fidelity gate; no merge or invented speedup.
