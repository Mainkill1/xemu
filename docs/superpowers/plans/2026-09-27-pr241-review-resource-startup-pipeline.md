# PR #241 Resource, Worker Startup, and Pipeline Review Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve the three current-head findings in [PR #241 comment 5854718472](https://github.com/Mainkill1/xemu/pull/241#issuecomment-5854718472) without weakening private-preview isolation.

**Architecture:** Keep the existing preview service and shared GL worker. Reject linked GL programs with active external resource interfaces before marking preparation successful. Confirm the worker's initial GL bind through a synchronized startup result, cancel a failed preparation request, and retry after a bounded delay. Build the finite set of Vulkan blend/depth/cull pipeline variants during paused preparation and only select an existing pipeline during render.

**Tech Stack:** C++, SDL3, epoxy/OpenGL, Vulkan, existing xemu preview service and production-linked lifecycle test.

**Spec:** PR #241 comment 5854718472 and `docs/superpowers/specs/2026-09-26-shader-workbench-design.md`.

## Global constraints

- The game never waits for preview render work and never uses preview-owned GL/Vulkan objects.
- Keep last-good images and three-slot retirement ownership on failed preparation.
- A GL test must actually link an SSBO shader on a capable 4.3+ context; compiler rejection on an older context does not count.
- The same worker startup path presents Vulkan output through GL.
- Render-state changes remain in the result key, not the source compile key.

## Review focus

- A linked shader with zero active ordinary uniforms but one shader-storage block is Unsupported before any draw.
- Existing last-good output and leases survive Unsupported and recovery.
- A failed first worker bind leaves no dead joinable worker or stuck preparation request; retry produces a frame in GL and Vulkan modes.
- No Vulkan graphics pipeline is created by a running preview render-state change.
- Paused preparation remains bounded in memory and cleanly destroys partially built pipelines on error.

### Task 1: GL resource-interface admission

**Files:** `ui/xui/shader-browser-preview-gl.cc`, `tests/unit/test-xemu-shader-browser-preview-lifecycle.cc`.

- [x] Add a production-linked SSBO fragment case after a valid displayed frame; assert Unsupported reason, unchanged last-good pixels, and unchanged logical slot ownership. Confirm the current code fails this assertion on a capable host.
- [x] Reject active shader-storage and atomic-counter interfaces when supported, and uniform blocks/subroutine inputs on their supported contexts; keep the existing default-uniform allowlist.
- [x] Run lifecycle OpenGL and Vulkan modes to show the GL path rejects the SSBO case and the normal path remains usable.

### Task 2: Worker startup acknowledgment and retry

**Files:** `ui/xui/shader-browser-preview-gl.{hh,cc}`, `ui/xui/shader-browser-preview-service.{hh,cc}`, `ui/xui/shader-browser-preview-service-slots.cc`, lifecycle and service tests.

- [x] Add a one-shot worker-bind failure in the production-linked lifecycle test and a service test for clearing a pending startup request. Confirm the current code fails.
- [x] Return the worker's first bind result through synchronized startup ownership; join and clear a failed worker before returning. Publish a retryable failure to the service and throttle repeated automatic retries.
- [x] Remove the injected fault and assert a real frame in both OpenGL and Vulkan-through-GL modes. Run service and lifecycle tests.

### Task 3: Bounded Vulkan state variants

**Files:** `ui/xui/shader-browser-preview-vk.cc`, lifecycle test and any focused Vulkan test needed.

- [x] Add a test that changes blend/depth/cull result state after preparation and observes no new graphics-pipeline creation while the guest is running. Confirm the current code fails.
- [x] Build the finite 24 target variants during paused preparation, clean all variants on failure/teardown, and select by clamped state in Render without driver pipeline creation.
- [x] Run native Vulkan scene and state-change smoke, then exact-head cross build and relevant focused tests.

### Task 4: Publication and remaining gates

- [ ] Review the final diff, run `git diff --check`, and record exact build/test hashes and platform limits.
- [ ] Push a non-force update to the existing PR #241 branch only after exact-head validation.
- [ ] Leave the PR draft until the broader CI, selected-shader workflow, and matched gameplay performance gates are met.
