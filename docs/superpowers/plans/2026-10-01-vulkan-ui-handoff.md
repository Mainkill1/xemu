# Vulkan UI Handoff Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce measured Vulkan frontend handoff work on Steam Deck without displaying stale output or changing guest timing.

**Architecture:** First measure the existing UI request, PFIFO lock, renderer synchronization, and frontend completion intervals using opt-in aggregate telemetry. Select one bounded change only if the trace attributes material time to avoidable handoffs. Keep the current synchronous path as the fallback for new output and invalidated resources.

**Tech Stack:** xemu C, Vulkan/OpenGL frontend, existing `XEMU_VK_PERF_LOG` JSONL telemetry, Steam Deck HTTP tester.

**Spec:** [Issue #90](https://github.com/Mainkill1/xemu/issues/90), particularly its September 2026 handoff and ownership updates.

## Global Constraints

- Current main already skips same-generation host-copy uploads; do not duplicate PR #101.
- Preserve PVIDEO, CPU framebuffer writes, resize, snapshot, renderer-switch, and resource lifetime behavior.
- Do not use guest flips, GL texture ID, or an unsynchronized generation read alone as freshness proof.
- Keep input polling responsive; do not add a presentation sleep or change guest clocks.
- Use the tester HTTP route and the same fixed workload and inputs for reference and candidate.
- Keep exact-head CI, full correctness, and per-test XISO timings as merge gates under `AGENTS.md`.

## Review Focus

- A framebuffer acquisition can return no surface: record its time without treating it as a valid sync request.
- Renderer-switch and shutdown can invalidate an output while the UI uses it: retain the current lease until GPU use completes.
- New output may arrive after a cached record is read: never skip the handshake without a synchronized publication contract.
- A CPU framebuffer write or PVIDEO update can change pixels without a guest flip: the fallback must still refresh it.
- Shared external-memory and host-copy paths have different ownership: qualify them independently.

### Task 1: Attribute the current handoff

**Files:** Modify `hw/xbox/nv2a/pgraph/vk/{renderer.c,renderer.h,perf.c}` and `ui/xemu.c` only where required by the selected counters.

- [ ] Add opt-in cumulative counts and microseconds for PFIFO lock acquisition, request-to-sync completion, and frontend `glFinish`; retain existing request and output counters.
- [ ] Build Linux x86_64 and run existing Vulkan unit tests; verify telemetry schema parses and counters remain zero when the path is not exercised.
- [ ] Run the same Deck scene with and without telemetry to bound observer overhead. Record shared/host-copy transport, guest progress, output builds, request count, wait totals, and UI work.
- [ ] Commit the focused attribution change and put raw evidence plus a current `HOLD` decision in the draft PR.

### Task 2: Choose and implement one measured reduction

**Files:** Depend on Task 1's measured owner; likely `ui/xemu.c`, `hw/xbox/nv2a/pgraph/{pgraph.c,vk/renderer.c,vk/renderer.h}` and focused tests under `tests/unit/`.

- [ ] Write a failing production-path test for the chosen freshness and lifetime rule, including new output and same-ID resource recreation.
- [ ] Verify that the test fails for the expected missing behavior.
- [ ] Implement the smallest synchronized handoff reduction; retain the existing synchronous path for invalidation and required freshness.
- [ ] Run the focused test, existing renderer-switch/display-output tests, Vulkan validation, and Linux/Windows builds.
- [ ] Commit the code separately from diagnostic telemetry.

### Task 3: Prove the effect and finish the draft

- [ ] Run order-balanced baseline/candidate pairs with identical title state and cache policy on Deck. Report guest cadence, p50/p95/p99/max intervals, CPU per guest work, request/wait reductions, output freshness, and input responsiveness.
- [ ] Run affected and control XISO leaves on OpenGL and Vulkan, retain per-test before/after time and correctness, then complete the broader required suite.
- [ ] Audit PVIDEO, CPU writes, resize, snapshot, renderer switch, paused UI, and shutdown. Report any unsupported host-copy qualification separately.
- [ ] Update the PR's first section to `MERGE`, `HOLD`, or `FAIL` from the actual evidence. Keep the PR draft while required gates remain open.
