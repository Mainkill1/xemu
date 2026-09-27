# Shader Workbench Reference Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bring the native PR #241 Shader Browser close to the user supplied tabbed workbench at 1440×900 and 1024×720.

**Architecture:** Keep the current ImGui browser, preview service, native renderers, and Stage 3 rule controls. Reallocate native window space: persistent catalog, compact workspace tabs and header, wide private scene, desktop side inspector or compact overlay, and lower editor dock. Use actual provider values throughout.

**Tech Stack:** C++, Dear ImGui, SDL3, existing xemu shader browser services.

**Spec:** User supplied `xemu-shader-workbench-tabs-handoff.zip` in PR #241 comment 5853458465; `AGENT-HANDOFF.md` and preview screenshots inside are the layout reference. Existing workbench behavior and constraints are in `docs/superpowers/specs/2026-09-26-shader-workbench-design.md`.

## Global Constraints

- Native UI only; no embedded HTML or fabricated shader measurements.
- Only explicit Stage 3 Apply/Restore changes game rendering.
- Preview output remains bounded by 640×480 and is private.
- Desktop reference is 1440×900; compact reference is 1024×720; catalog overlay threshold is 920 px.
- Preserve selection, draft, scene, inspector and editor state across primary tabs.

## Review Focus

- A 1024×720 window still has a useful preview and no clipped game actions.
- Resizing never overflows the private preview extent or changes source compile identity.
- A frozen reference and current image both fit within the scene area.
- Pausing/closing the browser does not leave private work running.
- A draft compile does not activate a game rule.

---

### Task 1: Layout geometry and native window

**Files:** `ui/xui/main.cc`, `ui/xui/shader-browser-part1.inc`, `ui/xui/shader-browser-part4.inc`, `ui/xui/shader-browser-preview-gl.{hh,cc}`; layout test in `tests/unit` if a pure helper is introduced.

- [x] Compare the pre-change native screenshot with both supplied reference sizes.
- [x] Use a 1440×900 initial external window within the display work area; keep it resizable.
- [x] Put the inspector in a 264 px right sidebar at desktop width and an overlay at compact width; leave the editor as a lower dock.
- [x] Render a wide private target within 640×480 and draw it to the available scene rectangle, including side by side comparison.
- [x] Compile and capture both viewport sizes with the native Windows executable.

### Task 2: Shared workspace and catalog

**Files:** `ui/xui/shader-browser-part1.inc`, `ui/xui/shader-browser-part2.inc`, `ui/xui/shader-browser-part3.inc`, `ui/xui/shader-browser-details-view.inc`.

- [x] Check current native screenshots against reference catalog, header, tabs, details, and settings.
- [x] Keep compact tabs and a persistent selected shader header, improve table legibility and summary spacing using real data.
- [x] Recheck native compact screenshots.
- [ ] Check keyboard navigation in the external workbench.

### Task 3: Validation and publication

**Files:** this plan and evidence report; code changes above.

- [ ] Run exact head compile and relevant test suite.
- [x] Validate native Vulkan and OpenGL preview, draft failure recovery, and resize on Windows across the reviewed revisions.
- [ ] Capture before/after evidence, commit and push the reviewed branch.
- [x] Leave PR draft until human validation gates and current PR text can be completed.
