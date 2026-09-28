# Shader inspection workflow implementation plan

> **For agentic workers:** Use superpowers:executing-plans for this authorized correction pass.

**Goal:** Inspect a supported game draw and its material inputs before experimenting, then inspect/edit GLSL and save an explicit game override.

**Architecture:** Keep the renderer's atomic ownership boundary. Unsupported emitted draws release a claim and continue the same cancellable search; each attempt has a distinct token. Backend snapshots own inputs. Preview workers consume copies. Existing profiling work is integrated selectively, preserving current capture adapters and UI fixes.

**Tech stack:** C/C++17, SDL3, Dear ImGui, OpenGL/Vulkan, Meson standalone unit tests and native Windows validation.

**Spec:** User feedback in this session: continuous supported capture, understandable preview wording, arrow-key auto selection, useful shader details/timing, stage explanations, obvious material extraction, game appearance before synthetic experimentation, GLSL editing and persistent replacements.

## Global constraints

- Stay on PR #259 and its #241 dependency; do not merge or start another PR.
- Keep the existing layout until the user's next reference arrives.
- Capture only actual emitted commands and resolved backend input generations.
- Never label synthetic material/vertex outputs as exact game appearance.
- Aim for at least 30 FPS in the preview; no recurring waits on a game queue.

## Review focus

- Unsupported draws before a supported draw must not terminate scanning.
- Cancel/rearm and delayed completion must not publish a different attempt.
- Keyboard focus must apply selection once without requiring Enter.
- Catalog drag and direct application must enforce title/stage/ABI compatibility.
- GPU timing must identify the complete draw interval, not falsely isolate a shader stage.

## Task 1: Continuous supported geometry capture

Files: shader-browser-draw-request.[h,hh,cc], shader-browser-part4.inc, test-xemu-shader-browser-draw-capture.cc.
- [ ] Add opt-in supported-geometry search, skipped count and distinct attempt tokens; test unsupported → supported and stale retry token rejection.
- [ ] Run standalone test red, implement, run green.
- [ ] Arm this mode from the UI; show search progress and cancel.

## Task 2: Replacement and keyboard interaction

Files: shader-browser-stage3-ui.cc, shader-browser-part2.inc, workbench editor/scene UI and selection widgets.
- [ ] Make catalog drag start directly after its selectable, before popup items can replace the source item.
- [ ] Explain target versus replacement package; provide a prominent direct apply action with compatibility reasons.
- [ ] Auto apply keyboard focused options and catalog rows; verify large selector navigation on the native UI.
- [ ] Remove visible private-test jargon; expose edit/save/apply workflow without changing rules during preview compilation.

## Task 3: Useful details and GPU costs

Files: profiling adapters and existing #242 model/provider work, shader-browser-part3.inc, details-view.inc.
- [ ] Selectively integrate monitoring/timestamp work from #242, resolving against current real-emission adapters.
- [ ] Show timings in us/ms and distinguish CPU work from sampled GPU draw interval.
- [ ] Explain VS/GS pipeline roles and current preview support; keep their GLSL inspectable.
- [ ] Build and run focused profiling tests and native samples on both backends.

## Task 4: Owned game material inputs

Files: capture request packet, GL/VK capture adapters, preview model/executors, workbench controls/export.
- [ ] Capture bound texture images, sampling state and shader constants from the resolved backend generation with bounded ownership.
- [ ] Preview/export these inputs explicitly and make captured inputs the game-inspection default.
- [ ] Retain honest approximation labels for missing vertex outputs, attachments or unsupported texture interfaces.
- [ ] Test packet bounds, ownership, cancellation and native capture/replay/export on both backends.

## Task 5: Validation and publication

- [ ] Format changes, run meaningful regressions, cross build exact head and native UI checks.
- [ ] Obtain fresh review; fix verified findings.
- [ ] Push to existing PR #259, obtain green exact-head CI and update validation evidence/pre-release when ready for human testing.
