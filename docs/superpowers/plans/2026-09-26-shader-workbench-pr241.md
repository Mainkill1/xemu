# Shader Workbench PR #241 Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development task by task. The user requests one batched final CI/native validation after the full plan is integrated; do not push or start GitHub CI between tasks.

**Goal:** Deliver issue #244's full synthetic shader workbench in PR #241 and mark the completed PR ready for user validation.

**Architecture:** Extend the existing PreviewScene, PreviewPacket, PreviewService, private GL/Vulkan executors, external browser UI, and Stage 3 override path. A fixed reference scene and editable draft are immutable inputs to one bounded preview service; a versioned owned experiment bundle saves the same inputs. No game-draw capture or second preview subsystem is used.

**Tech Stack:** C++17, Dear ImGui, OpenGL, Vulkan, SDL, Meson, existing xemu shader recipe/detail/override libraries.

**Spec:** `docs/superpowers/specs/2026-09-26-shader-workbench-design.md`; source issue https://github.com/Mainkill1/xemu/issues/244.

## Global Constraints

- Use isolated workhub Git units. The active PR checkout is read-only; integrate only reviewed commits into the dedicated integration unit.
- Workers are not alone in the codebase. Each worker owns the files named below, preserves other workers' edits, and stops on an ownership conflict.
- No real game-draw capture; every workbench result and export says `Synthetic inputs — not an in-game draw`.
- Generated GLSL is xemu translation with resident/reconstructed provenance; canonical guest recipes and authored drafts remain distinct.
- Three leased output slots, one newest active work item, 32 MiB owned input cap, hidden/disabled and pressure rules, and no gameplay waits/cache/stat mutations remain intact.
- The build machine is compile-only. Native testing uses Steam Deck and Windows hardware. Windows test files stay in the dedicated test directory.
- No intermediate push or CI. Perform only local focused checks needed to catch concrete implementation risks; batch full native tests, gameplay comparison, and GitHub CI after all tasks integrate.
- Follow AGENTS.md commit format, xemu code style, agent declaration, and Mainkill1-only GitHub push safety scan. Do not merge the PR.

## Review Focus

1. A fragment shader discards the target: camera focus and reference objects remain usable, and no old target pixels survive a depth/color clear.
2. A rapid edit, selection change, or renderer reset arrives during private compile: only the matching newest revision can display; last-good is labeled with its exact stale conditions.
3. A generated source is reconstructed or comes from a different title/build: Clone, Export, and Apply preserve/reject provenance and exact scope rather than using the first same-title entry.
4. A malformed or oversized test bundle includes nonfinite floats, wrong hashes, missing companion source, or path tricks: parse rejects it before large allocation or any game action.
5. The external window has focus while the guest runs or is paused: preview mouse/keyboard gestures do not trigger guest controls; closing/reopening does not leak GL/Vulkan resources.

## Task 1 — Bounded shared-scene contract (scene lane)

**Files:** `ui/xui/shader-browser-preview-scene.{hh,cc}`, new `shader-browser-workbench-scene-ui.inc`, `tests/unit/test-xemu-shader-browser-preview-scene.cc`, `docs/devel/shader-browser-stage4.md` (scene section only).

**Interface:** Define `PreviewReferenceId` for backdrop, ground, intersection, blocker; `PreviewReference` with visibility and bounded translation; extend `PreviewScene` with version and target pivot. Define `PreviewSceneDraw` (`role`, `first_vertex`, `vertex_count`) and `PreviewSceneFrame` (`vertices`, fixed draw records). Define `PreviewRenderState` in the scene header; the editor lane owns its connection to packet/result keys. Add `BuildPreviewSceneFrame(const PreviewScene &, float aspect, bool vulkan)` and `ApplyPreviewCameraGesture(PreviewScene, PreviewCameraGesture)`. Keep `BuildPreviewSceneGeometry` as a compatibility wrapper until both backends migrate. Clamp nonfinite values and enforce one explicit maximum vertex count and extent.

- [ ] Add behavior tests for all fixed roles, draw-range bounds, known near/far ordering, quad/sphere/cube target, visibility/translation, focus pivot, camera gestures, finite clamping, and GL/Vulkan aspect/projection parity.
- [ ] Implement the immutable frame builder and camera gesture helper. Preserve fixture UV/color on target; reference materials use deterministic colors/patterns and cannot inherit the selected shader.
- [ ] Review scene output and budget; record exact vertex and byte cap in docs. Run a local focused check if the geometry contract is in doubt; no CI.

## Task 2 — Shared private GL and Vulkan depth execution (scene lane)

**Files:** `ui/xui/shader-browser-preview-{gl,vk}.cc`, related `.hh` only if needed, native GL/Vulkan scene/lifecycle tests. No editor or bundle files.

**Interface:** Consume Task 1 `PreviewSceneFrame` and `PreviewRenderState` (synthetic blend/depth-test/depth-write/cull/alpha settings and destination clear). The editor lane connects this type to packet/result keys; static Vulkan pipeline variants are keyed separately from GLSL source. Use one checked, bounded depth attachment per serial private renderer; all draw records share the same color/depth pass. Draw references with private fixed shaders/pipelines and the target with the selected shader; clear attachments for each result. Preserve three color output leases.

- [ ] Add targeted native pixel assertions for blocker occlusion, intersection, ground/background, target-only shader variation, depth clear, alpha/discard, and camera movement on GL and Vulkan.
- [ ] Implement GL renderbuffer/reference program and Vulkan depth image/render-pass/reference pipeline with explicit supported-format admission and destruction order. Keep selected source preparation independent from camera/input edits.
- [ ] Add resource-bound/reset/resize failure tests and documentation of known unsupported combinations. Defer the full native matrix until integration.

## Task 3 — Generated source and private draft model (editor lane)

**Files:** new `ui/xui/shader-browser-workbench-draft.{hh,cc}` and tests; `shader-browser-details-model.hh`/detail bridge only for missing source ABI metadata; `shader-browser-preview-{adapter,model,service}*.{hh,cc}` and their tests. Coordinate shared packet/render-state fields with scene lane; no GL/VK renderer edits.

**Interface:** `GeneratedSourceSnapshot` owns full key/scope/backend/stage/route, resident/reconstructed flag, generator/interface ABI, source digest/text. `WorkbenchDraft` owns immutable base, editable text, ID/revision, 400 ms debounce state, requested/last-successful revision, and diagnostics. `FreezeCompile()` returns an owned newest revision. `PreviewSourceVariant {Original, Edited}` is separate from Stage 3 Replacement. Compile identity includes source variant/draft ID/revision/digest; result identity includes scene, fixture, clock sample, render state and extent. Expose attempted and displayed source/revision status to UI.

- [ ] Add tests for exact-scope clone, immutable original, revision/debounce/manual compile, invalid source retaining exact last-good label, stale completion rejection, no recompilation on camera/uniform changes, and unchanged three-slot/pressure/hidden rules.
- [ ] Implement source provenance transfer, private draft lifecycle and packet/service identity. Never fabricate Stage 3 replacement IDs or submit an override from preview compilation.
- [ ] Add declared clock-to-input bindings with stable names and saved values; a shader that ignores the driven input may remain static. Report missing/unsupported varyings and texture interfaces precisely.

## Task 4 — Compact editor and viewport interaction (editor lane, then integrator)

**Files:** new `ui/xui/shader-browser-glsl-editor.{hh,cc}` and tests; new `shader-browser-workbench-editor-ui.inc`; integrator later owns `shader-browser-part4.inc`, `shader-browser.hh`, and Meson include wiring. The editor lane must not directly edit shared Part 4 UI. The scene lane owns `shader-browser-workbench-scene-ui.inc`.

**Interface:** Editor widget exposes source text/revision, visible-line token colors, line numbers, search match/next, and compiler diagnostic line navigation. It handles up to 4 MiB without per-frame full-source work. UI wrapper shows Generated provenance, Create editable copy, hideable dock, Manual Compile, auto-compile, Original/Edited, attempted/displayed revision, and exact compiler feedback. The scene lane's wrapper handles viewport-focused orbit/pan/dolly/F focus/reset, object visibility/translation, render controls, and collapsed Inputs/State/Textures drawers; integrator connects both wrappers to the existing external window and service.

- [ ] Add focused tests for line indexing, UTF-8/newline/large-source search, GLSL token ranges, compiler log line extraction/fallback, and gesture ownership/scene result revision.
- [ ] Implement widget and wrappers with explicit focus capture, no guest input leak, no 2D image zoom substitution, and camera persistence across source/selection changes.
- [ ] Integrator performs one shared UI insertion after scene and editor worker commits are reviewed; verify 1024x720 layout and paused interaction in final native pass.

## Task 5 — Test bundle, GLSL/usage exports, and table (export lane)

**Files:** new `shader-browser-workbench-bundle.{hh,cc}` and test, new `shader-browser-usage-export.{hh,cc}` and test, `shader-browser-model.{hh,cc}`, `shader-browser-part2.inc`, related model tests. No Part 4 or private renderer edits.

**Interface:** `WorkbenchExperiment` is a bounded owned DTO consuming Task 1 scene and Task 3 source/draft/input contracts. `SerializeWorkbenchExperiment` / `ParseWorkbenchExperiment` use schema `xemu.shader-workbench-test.v1`; parser validates size/digest/finite values/ABI and cannot activate overrides. Export uses a unique `.xemu-test.json` file under shader-exports and atomic temp/rename; Reopen restores an inactive owned experiment before normal admission. Copy/Export GLSL is byte-exact and has a provenance sidecar distinct from canonical recipe export. Usage CSV/JSON formats one copied Snapshot without invented timings. Table filters use full-key pin/hide, frame-relative recency and stable selection; compact columns fit 1024x720.

- [ ] Add round-trip and rejection tests for exact full key/build, source/draft/fixture/scene/clock/render state, owned texture bytes, float precision, wrong schema/size/digest/partner, and no override side effect.
- [ ] Add deterministic CSV/JSON and table tests for quoting, null measurements, pin order, hidden/show-hidden, recent boundary/underflow, and filtered selection stability.
- [ ] Implement pure serializers/formatters/model first, then file/clipboard and table UI actions. Coordinate DTO adapters only after Task 1 and 3 interfaces settle.

## Task 6 — Explicit Stage 3 Apply and Restore (export lane)

**Files:** `shader-browser-stage3-ui.{hh,cc}`, `shader-browser-replacement-library.{hh,cc}` only where needed; new pure apply preflight helper/test; existing Stage 3 scoped/override tests. Integrator owns the final editor button wiring.

**Interface:** `EvaluateDraftGameApply` checks successful current draft, pixel stage, interface ABI 1, backend, full key and exact title/build scope plus Stage 3 package compatibility. Apply writes an immutable authored replacement package and calls the existing Stage 3 rule action; Restore removes only that rule. Requested/effective action and revision remain separate from private preview status.

- [ ] Add tests for stage/ABI/backend/build/revision mismatch, preview compile with no game action, explicit Apply scope, effective-state report, and specific Restore.
- [ ] Implement package creation and Stage 3 bridge without changing canonical recipes or using disposable artifact cleanup.
- [ ] Leave unsupported nonpixel drafts inspectable/exportable with a precise game-Apply reason.

## Task 7 — Integration and final qualification (integrator lane)

**Files:** shared `shader-browser-part4.inc`, `shader-browser.hh`, `ui/xui/meson.build`, overall Stage 4 docs/evidence and PR description; targeted cross-lane fixes only after owner review.

- [ ] Integrate reviewed scene, editor, and export units one at a time into the dedicated integration unit; resolve interface/Meson/UI conflicts, update status wording to synthetic-only, and run privacy/diff scans locally.
- [ ] Connect external-window viewport, editor dock, input/render drawers, test reopen, Copy/Export GLSL, usage exports, Apply/Restore, and exact status labels. Verify no hidden user action writes a game override.
- [ ] After all implementation is present, run one batched full unit/build gate and native GL/Vulkan matrix on Deck and Windows. The build server may compile but run no test. Verify 1024x720 layout, paused focus isolation, source errors/last-good, rapid edits/selection/reset, scene depth pixels, export/reopen, bounded resources, and matched gameplay preview-off/on frame logs.
- [ ] Review final full diff independently, fix findings, run Mainkill1 privacy scan, push the exact final head once, and run GitHub CI once. Update PR body with agent/model declarations and honest native evidence; mark PR ready only when all gates pass. Do not merge.
