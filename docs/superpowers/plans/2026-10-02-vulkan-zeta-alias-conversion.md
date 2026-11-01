# Vulkan Zeta Alias Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a validated GPU primitive that converts guest Morton-ordered Z24S8 words into a linear packed depth buffer, ready for the later read-only alias ownership change.

**Architecture:** Keep the guest-layout mapping and shader source in a small dedicated module. Reuse the existing three-storage-buffer Vulkan compute descriptor layout and pipeline cache, adding one explicit compute operation. This plan does not select or retain surface aliases, so existing draw behavior remains unchanged.

**Tech Stack:** C, GLib, Vulkan compute, xemu's glslang compiler and QEMU Meson unit tests.

**Spec:** `docs/superpowers/specs/2026-10-02-conker-gpu-zeta-alias-design.md` (first reviewable milestone).

## Global Constraints

- Existing depth pack and unpack behavior must remain unchanged.
- Input and output contain `width * height` packed 32-bit Z24S8 words; only nonzero power-of-two dimensions are eligible.
- A guest-layout source word at Morton offset `morton(x,y,width,height)` becomes destination word `y * width + x`.
- No GPU→RAM readback suppression or alias lifetime change is part of this PR.
- Keep this as a separate stacked PR on #284, with HOLD until its successor integrates and validates the conversion.

## Review Focus

- Non-square 16×32 and 32×16 Morton maps must follow the CPU `swizzle_box` routine.
- Out-of-range coordinates and zero/non-power-of-two dimensions must be rejected before GPU dispatch.
- Existing pack/unpack pipeline keys must not collide with the new alias operation.
- Repeated alias dispatches must not overwrite descriptor sets still referenced by a command buffer.
- Source and destination buffers that alias or are too short must be rejected by the future call site, not silently accepted as a valid conversion.

---

### Task 1: Guest-layout mapping and shader source

**Files:**
- Create: `hw/xbox/nv2a/pgraph/vk/surface-alias-map.h`
- Create: `hw/xbox/nv2a/pgraph/vk/surface-alias-map.c`
- Modify: `tests/unit/test-xbox-vk-ubershader-glsl-integration.c`
- Modify: `tests/unit/meson.build`

**Interfaces:**
- Produces: `bool pgraph_vk_alias_morton_index(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t *index)`.
- Produces: `char *pgraph_vk_alias_unswizzle_glsl(unsigned int workgroup_size)`, owned by the caller.

- [ ] Add a failing mapping test comparing the returned index with `swizzle_box` for 32×32, 16×32, and 32×16; reject invalid shapes and coordinates.
- [ ] Run `ninja -C build tests/unit/test-xbox-vk-ubershader-glsl-integration` and the new focused test; confirm failure because the new API is absent.
- [ ] Implement the two functions. The GLSL shader reads packed words at Morton offsets and writes linear destination indices; use the existing two-`uint` push-constant layout for width and height.
- [ ] Add a failing shader-compile test using `pgraph_vk_compile_glsl_to_spv_config(..., GLSLANG_STAGE_COMPUTE, ...)`; then compile the generated source for workgroup sizes 1 and 256.
- [ ] Run the focused test and confirm the mapping and both shader variants pass.
- [ ] Commit the mapping and shader-source module independently.

### Task 2: Wire the conversion into the existing compute cache

**Files:**
- Modify: `hw/xbox/nv2a/pgraph/vk/renderer.h`
- Modify: `hw/xbox/nv2a/pgraph/vk/surface-compute.c`
- Modify: `tests/unit/test-xbox-vk-ubershader-glsl-integration.c`

**Interfaces:**
- Consumes: Task 1's GLSL generator and Morton shape contract.
- Produces: `void pgraph_vk_unswizzle_packed_z24s8(PGRAPHState *pg, VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t width, uint32_t height)`; callers provide a write-after-read barrier and confirm descriptor/buffer capacity before calling.

- [ ] Add a failing test that distinguishes the alias operation from existing pack and unpack compute pipeline keys without changing the existing operations' identities.
- [ ] Replace the key's boolean pack flag with an explicit pack/unpack/alias operation enum, and route only the new operation to Task 1's GLSL source.
- [ ] Add the dispatch function using one descriptor set per dispatch and `width * height` work items; bind all three descriptors required by the existing layout, with the unused middle descriptor pointing to the source buffer.
- [ ] Run the focused unit test, `test-xbox-vk-surface-coherence`, and `test-xbox-pgraph-zeta-write`; confirm pass.
- [ ] Build `qemu-system-i386`, run `git diff --check`, and commit the compute-cache integration.

### Task 3: Validate the primitive before alias ownership work

**Files:** No product changes. Keep evidence with this branch and the later owning xemu PR.

- [ ] Compile generated compute GLSL and validate emitted SPIR-V through the maintained compiler test on Linux, Windows, and macOS CI.
- [ ] Review generated shader bounds against the CPU mapping test and confirm a future call site cannot pass unsupported dimensions or undersized buffers without an explicit check.
- [ ] Publish this branch as a small stacked draft PR with its exact-head CI and the absence of a runtime performance claim. Implement alias ownership under a separate plan/PR after the GPU primitive is reviewable.
