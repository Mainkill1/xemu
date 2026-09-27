# Shader Draw and Object Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a tested identity, segmentation, resource-edge, and conservative object-grouping foundation for shader-seeded game draw capture.

**Architecture:** Keep exact draw/resource facts separate from inferred object membership. Operate on draw segments, automatically merge only exact same-pass evidence, and surface weaker relationships as suggestions or resource-only edges.

**Tech Stack:** C++17, existing Shader Browser model types, Meson focused unit executables.

**Spec:** `docs/superpowers/specs/2026-09-27-shader-draw-object-capture-design.md`

## Global Constraints

- Stack on `feature/shader-browser-stage4`.
- Do not add live PGRAPH copies or gameplay waits in this foundation PR.
- Same shader or same texture alone must never establish object identity.
- One draw may produce zero, one, or many explicit segments; zero uses a whole-draw fallback.
- Weak grouping remains a suggestion; only exact same geometry plus transform/skinning may merge automatically.

## Review Focus

- Guest address reuse with changed content must not alias one resource.
- Equal bytes at different addresses must not become one allocation identity.
- Shared shader usage across unrelated objects must produce separate candidates.
- One batched draw with disconnected segments must remain split without stronger evidence.
- Different transforms must block grouping even when a vertex buffer is shared.

---

### Task 1: Draw and resource identity model

**Files:**
- Create: `ui/xui/shader-browser-draw-capture.hh`
- Create: `ui/xui/shader-browser-draw-capture.cc`
- Test: `tests/unit/test-xemu-shader-browser-draw-capture.cc`

**Interfaces:**
- Produces: `DrawEventKey`, `DrawSegmentKey`, `ResourceIdentity`, `DrawCaptureSummary`, `DrawUsesShader()`, `FindDrawsUsingShader()`, and `EffectiveDrawSegments()`.

- [ ] Write assertions for exact shader matching, multiple matching draws, whole-draw fallback, strict address/content identity, and address reuse rejection.
- [ ] Compile before implementation and confirm the missing capture header/source fail the test.
- [ ] Implement the minimal identity and lookup model.
- [ ] Compile with `-std=c++17 -Wall -Wextra -Werror` and run the focused executable.

### Task 2: Draw segmentation

**Files:**
- Modify: `ui/xui/shader-browser-draw-capture.hh`
- Modify: `ui/xui/shader-browser-draw-capture.cc`
- Test: `tests/unit/test-xemu-shader-browser-draw-capture.cc`

**Interfaces:**
- Consumes: draw segment identities from Task 1.
- Produces: `PrimitiveSegmentation SegmentTriangleList(const std::vector<uint32_t>&)`.

- [ ] Add failing assertions for connected triangles, disconnected islands, degenerate separators, and incomplete trailing indices.
- [ ] Implement deterministic union-find segmentation over non-degenerate triangle-list primitives.
- [ ] Verify the focused executable passes.

### Task 3: Conservative object graph

**Files:**
- Modify: `ui/xui/shader-browser-draw-capture.hh`
- Modify: `ui/xui/shader-browser-draw-capture.cc`
- Test: `tests/unit/test-xemu-shader-browser-draw-capture.cc`

**Interfaces:**
- Consumes: draw summaries, segments, and resource identities.
- Produces: `AnalyzeObjectRelationship()`, `BuildObjectCandidates()`, and `TraceShaderObjectUsage()`.

- [ ] Add failing assertions for same-pass grouping, unrelated same-shader draws, shared-buffer/different-transform blocking, attached-part suggestions, and one-draw/multiple-segment separation.
- [ ] Implement inspectable evidence flags and conservative scoring.
- [ ] Merge only exact same-pass edges; retain weaker object and resource relationships separately.
- [ ] Verify the focused executable passes with warnings treated as errors.

### Task 4: Build and developer documentation

**Files:**
- Modify: `ui/xui/meson.build`
- Create: `docs/devel/shader-draw-object-capture.md`
- Create: `docs/superpowers/specs/2026-09-27-shader-draw-object-capture-design.md`
- Create: `docs/superpowers/plans/2026-09-27-shader-draw-object-capture.md`

**Interfaces:**
- Consumes: the focused source and test from Tasks 1–3.
- Produces: production-source registration, a unit target, and a staged integration contract.

- [ ] Register `shader-browser-draw-capture.cc` in `xemu_ss`.
- [ ] Add `test-xemu-shader-browser-draw-capture` to the focused unit suite.
- [ ] Document exact versus inferred identity, current implementation, limitations, and the backend integration sequence.
- [ ] Run source-level compilation locally and let GitHub CI perform the full repository build because the sandbox cannot clone the repository.
