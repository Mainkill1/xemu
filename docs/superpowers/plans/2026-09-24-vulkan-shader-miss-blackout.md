# Vulkan Shader-Miss Blackout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in Vulkan policy that keeps guest execution responsive when a required shader or graphics pipeline is missing by asynchronously building the exact executable, omitting the unrenderable draw, and presenting a black game image until rendering can resume.

**Architecture:** Keep PR #203 as the investigation and handoff record; implement the feature as small stacked product PRs. First make Vulkan draw completion explicit, then generalize the existing asynchronous shader path to vertex/geometry/fragment stages, retain complete demand-executable requests until their pipeline is published, add a safe omitted-draw boundary, and only then add host blackout presentation and the Advanced setting. The ordinary `Wait` path remains the correctness reference and must not change behavior.

**Tech Stack:** C11/QEMU-style C, C++ UI, Vulkan, glslang, GLib tests, Meson, existing xemu hybrid compiler/pipeline worker and persistent cache infrastructure.

**Spec:** `docs/performance/issue-198-ubershader-loading.md`

## Global Constraints

- Audit/implementation base is `134de6616e1d8f5bfe9d919a4e98e0ff7da3c928`; rebase each child PR onto the then-current `main` and record the new base explicitly.
- Keep `Shader miss handling = Wait` as the default; existing saved ubershader choices remain intact.
- Do not implement a replacement black fragment shader. An omitted draw plus an already-ready host black presentation path is the intended escape.
- `Continue with black frames` must never silently fall back to synchronous shader or pipeline compilation when its queue is full or a worker is unavailable.
- Never replay a skipped draw after guest execution advances. Later matching draws may use the completed executable; one-shot lost rendering remains an explicitly accepted limitation.
- Preserve exact `ShaderModuleCacheKey`, `PipelineKey`, generation and ticket validation. Never publish stale worker output or approximate unsupported guest state.
- Shader workers own immutable source/config bytes only. Vulkan cache publication and object ownership remain on the renderer thread.
- Do not mark color, depth, stencil, query or report effects as completed for an omitted draw.
- Do not claim the feature removes texture uploads, surface readbacks, guest I/O, CPU emulation stalls or presentation stalls; `create_pipeline()` currently binds textures before shader-route resolution.
- Keep product changes out of umbrella PR #203. Open focused child PRs that reference #198 and #203, and attach their evidence beneath the directory selected by `EVIDENCE_DIR=evidence/wiki-xiso-per-test/pr-${CHILD_PR_NUMBER}/issue-198` after the child PR number is assigned.
- Every child PR must include an agent/model declaration and must follow `AGENTS.md` and `CONTRIBUTING.md`.

## Review Focus

1. **Repeated missing draw with no fallback:** it must remain nonblocking, deduplicate exact work, avoid staging growth, and eventually use the published executable on a later matching draw. Covered by Tasks 3 and 4.
2. **Queue saturation or permanent worker failure:** it must remain visibly `deferred` or `failed`, never claim compilation is active, and never switch to blocking. Covered by Tasks 2, 3 and 6.
3. **Omitted indexed/inline draw after vertex preparation:** it must not dirty surfaces, advance draw generations, leave private-mirror pages stale, or leak staging capacity. Covered by Tasks 1 and 4.
4. **Blackout while no guest flip succeeds:** compiler and pipeline completions must still be serviced and published; blackout acquisition must not request a synchronized guest framebuffer first. Covered by Tasks 3 and 5.
5. **Renderer reset/backend switch/shutdown with pending demand work:** workers must stop, owned Vulkan objects must be destroyed or published exactly once, and stale generation/ticket results must be ignored. Covered by Tasks 2, 3 and 5.

---

## Required PR Decomposition

Do not turn this plan into one large product PR.

| Child PR | Tasks | Mergeable result |
| --- | --- | --- |
| A — draw result contract | Task 1 | Vulkan distinguishes submitted, intentionally omitted and failed draws without changing current runtime behavior. |
| B — generic asynchronous modules | Task 2 | Existing hybrid compiler safely compiles and publishes vertex, geometry and fragment modules. |
| C — retained demand executable and omission | Tasks 3–4 | Missing exact executables are queued without a drawable fallback and affected draws can be intentionally omitted without corrupting renderer state. |
| D — blackout policy and UI | Tasks 5–6 | Host output becomes black without guest framebuffer synchronization, with an explicit default-Wait Advanced policy and truthful status. |
| E — qualification | Task 7 | Exact builds, cold/warm game runs, correctness controls and evidence support the final decision. |

Each child PR should target current `main`; later child PRs may temporarily stack on the immediately preceding branch for testing, but must state the dependency and be restacked after the parent merges.

### Task 1: Introduce an explicit Vulkan draw lifecycle result

**Files:**
- Create: `hw/xbox/nv2a/pgraph/vk/draw-lifecycle.h`
- Create: `hw/xbox/nv2a/pgraph/vk/draw-lifecycle.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/draw.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/meson.build`
- Create: `tests/unit/test-xbox-vk-draw-lifecycle.c`
- Modify: `tests/unit/meson.build`

**Interfaces:**
- Consumes: existing `PGRAPHState`, `PGRAPHVkState`, `SurfaceBinding`, `pgraph_vk_set_surface_dirty()`.
- Produces:

```c
typedef enum PGRAPHVkDrawPrepareResult {
    PGRAPH_VK_DRAW_PREPARE_READY,
    PGRAPH_VK_DRAW_PREPARE_OMITTED_SHADER_MISS,
    PGRAPH_VK_DRAW_PREPARE_FAILED,
} PGRAPHVkDrawPrepareResult;

typedef enum PGRAPHVkDrawResult {
    PGRAPH_VK_DRAW_SUBMITTED,
    PGRAPH_VK_DRAW_OMITTED_SHADER_MISS,
    PGRAPH_VK_DRAW_FAILED,
} PGRAPHVkDrawResult;

void pgraph_vk_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHVkState *r, PGRAPHVkDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty);
```

- [ ] **Step 1: Write the lifecycle tests before changing `draw.c`**

Mirror the established OpenGL production helper/test pattern in `gl/draw-lifecycle.{c,h}` and `test-xbox-gl-draw-lifecycle.c`. Add these exact Vulkan cases:

```c
static void test_omitted_draw_does_not_publish_surface_generation(void);
static void test_failed_draw_does_not_publish_surface_generation(void);
static void test_submitted_draw_publishes_surface_generation(void);
```

For omitted/failed, assert unchanged `pg.draw_time`, unchanged color/zeta `draw_time`, and zero `pgraph_vk_set_surface_dirty()` calls. For submitted, assert one generation increment and one dirty call.

- [ ] **Step 2: Register and run the red test**

Add `test-xbox-vk-draw-lifecycle` to `tests/unit/meson.build`, linking the new production source.

Run:

```bash
BUILD_DIR=build
meson test -C "$BUILD_DIR" --print-errorlogs test-xbox-vk-draw-lifecycle
```

Expected before implementation: build failure because `draw-lifecycle.h` and its functions do not exist.

- [ ] **Step 3: Implement the minimal lifecycle helper**

Follow the OpenGL helper: only `PGRAPH_VK_DRAW_SUBMITTED` advances `pg->draw_time`, updates bound surface generations, and calls `pgraph_vk_set_surface_dirty()`. Omitted and failed results return without publishing guest-visible GPU writes.

- [ ] **Step 4: Propagate typed results through `draw.c` without changing behavior**

Change `begin_pre_draw()` to return `PGRAPHVkDrawPrepareResult`; map the current `create_pipeline()` false result to `PGRAPH_VK_DRAW_PREPARE_FAILED`. Change `pgraph_vk_flush_draw_internal()` to return `PGRAPHVkDrawResult`; all currently successful paths return `SUBMITTED`, existing hard failures return `FAILED`, and the existing empty-method path also remains `SUBMITTED` so this contract-only PR does not alter its historical draw-time behavior. Replace the direct completion logic in `pgraph_vk_draw_end()` with `pgraph_vk_complete_draw_lifecycle()`.

Do not emit `"texture preparation failed"` for the future omitted result. Hard failures remain diagnosable; intentional omission gets its own counter in Task 4.

- [ ] **Step 5: Run focused and existing lifecycle tests**

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xbox-vk-draw-lifecycle \
  test-xbox-gl-draw-lifecycle \
  test-xbox-vk-ubershader-runtime
```

Expected: all selected tests pass; current runtime behavior remains unchanged because no code produces `OMITTED_SHADER_MISS` yet.

- [ ] **Step 6: Commit Child PR A**

```bash
git add \
  hw/xbox/nv2a/pgraph/vk/draw-lifecycle.c \
  hw/xbox/nv2a/pgraph/vk/draw-lifecycle.h \
  hw/xbox/nv2a/pgraph/vk/draw.c \
  hw/xbox/nv2a/pgraph/vk/meson.build \
  tests/unit/test-xbox-vk-draw-lifecycle.c \
  tests/unit/meson.build
git commit -m "vk: distinguish omitted draw completion"
```

### Task 2: Generalize asynchronous module compilation to all graphics stages

**Files:**
- Modify: `hw/xbox/nv2a/pgraph/vk/shaders.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/renderer.h`
- Modify: `tests/unit/test-xbox-vk-hybrid-compiler.c`
- Modify: `tests/unit/test-xbox-vk-ubershader-glsl-integration.c`
- Modify: `tests/unit/test-xbox-vk-ubershader-runtime.c`

**Interfaces:**
- Consumes: `PGRAPHVkHybridCompiler`, `PGRAPHVkHybridShaderWork`, `ShaderModuleCacheKey`, existing GLSL generators and `materialize_hybrid_shader_module()`.
- Produces:

```c
typedef enum PGRAPHVkAsyncModuleRequestResult {
    PGRAPH_VK_ASYNC_MODULE_READY,
    PGRAPH_VK_ASYNC_MODULE_ACCEPTED,
    PGRAPH_VK_ASYNC_MODULE_DUPLICATE,
    PGRAPH_VK_ASYNC_MODULE_DEFERRED,
    PGRAPH_VK_ASYNC_MODULE_FAILED,
} PGRAPHVkAsyncModuleRequestResult;

PGRAPHVkAsyncModuleRequestResult pgraph_vk_request_shader_module_async(
    PGRAPHState *pg, const ShaderModuleCacheKey *key);
```

- [ ] **Step 1: Add red tests proving stage identity is preserved**

In `test-xbox-vk-hybrid-compiler.c`, submit one vertex, one geometry and one fragment request with distinct source bytes. Assert that each completion returns the original `stage`, generation and ticket, and that duplicate detection distinguishes equal source at different stages.

In `test-xbox-vk-ubershader-glsl-integration.c`, add a table-driven compile check for generated vertex, required geometry and fragment GLSL using the production compile callback.

- [ ] **Step 2: Run the tests before changing completion publication**

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xbox-vk-hybrid-compiler \
  test-xbox-vk-ubershader-glsl-integration
```

Expected: the raw queue stage tests may pass because the queue is generic; the production integration/publication test must fail or remain incomplete because `pgraph_vk_process_hybrid_completions()` rejects every non-fragment result.

- [ ] **Step 3: Add one stage mapping and source-generation path**

Use the module key as the authority:

```c
static glslang_stage_t shader_module_glslang_stage(
    VkShaderStageFlagBits kind)
{
    switch (kind) {
    case VK_SHADER_STAGE_VERTEX_BIT:
        return GLSLANG_STAGE_VERTEX;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        return GLSLANG_STAGE_GEOMETRY;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        return GLSLANG_STAGE_FRAGMENT;
    default:
        g_assert_not_reached();
    }
}
```

Add a single helper that generates exact GLSL for `ShaderModuleCacheKey.kind`, adopts a persistent SPIR-V hit when available, deduplicates existing key/source work, and otherwise submits through the existing bounded compiler. Replace the two fragment-only submission implementations with this helper; retain their route-specific policy around it.

- [ ] **Step 4: Generalize completion validation safely**

Replace `result.stage != GLSLANG_STAGE_FRAGMENT` with an exact comparison against `shader_module_glslang_stage(work->module_key.kind)`. Compute trace route without reading the inactive `psh` union member for vertex or geometry keys. Materialize through the existing `work->module_key.kind`, then add the artifact to the persistent cache with that same kind.

Do not change the compiler worker threading model. It already owns source/config bytes and can carry arbitrary glslang stages; this task changes the production callers and result consumer.

- [ ] **Step 5: Test stale completion, duplicate and shutdown cases for every stage**

Extend runtime tests so an old generation result for each stage is discarded, matching work is cleared only after successful publication, and a stopped compiler reports `FAILED`/`DEFERRED` without a synchronous retry.

Run:

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xbox-vk-hybrid-compiler \
  test-xbox-vk-ubershader-glsl-integration \
  test-xbox-vk-ubershader-runtime \
  test-xbox-vk-spirv-prewarm
```

Expected: all stage, ownership and cache tests pass.

- [ ] **Step 6: Commit Child PR B**

```bash
git add \
  hw/xbox/nv2a/pgraph/vk/shaders.c \
  hw/xbox/nv2a/pgraph/vk/renderer.h \
  tests/unit/test-xbox-vk-hybrid-compiler.c \
  tests/unit/test-xbox-vk-ubershader-glsl-integration.c \
  tests/unit/test-xbox-vk-ubershader-runtime.c
git commit -m "vk: compile demanded graphics shader stages asynchronously"
```

### Task 3: Retain and service complete demand-executable requests

**Files:**
- Create: `hw/xbox/nv2a/pgraph/vk/demand-executable.h`
- Create: `hw/xbox/nv2a/pgraph/vk/demand-executable.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/renderer.h`
- Modify: `hw/xbox/nv2a/pgraph/vk/shaders.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/draw.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/renderer.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/meson.build`
- Create: `tests/unit/test-xbox-vk-demand-executable.c`
- Modify: `tests/unit/test-xbox-vk-ubershader-runtime.c`
- Modify: `tests/unit/meson.build`

**Interfaces:**
- Consumes: generic async module requests from Task 2, `PipelineKey`, ready-binding probes, `request_hybrid_pipeline()`, pipeline completion publication, renderer generation.
- Produces:

```c
#define PGRAPH_VK_MAX_DEMAND_EXECUTABLES 64U

typedef enum PGRAPHVkDemandExecutableStatus {
    PGRAPH_VK_DEMAND_WAITING_FOR_MODULES,
    PGRAPH_VK_DEMAND_WAITING_FOR_BINDING,
    PGRAPH_VK_DEMAND_PIPELINE_PENDING,
    PGRAPH_VK_DEMAND_READY,
    PGRAPH_VK_DEMAND_DEFERRED,
    PGRAPH_VK_DEMAND_FAILED_PERMANENT,
} PGRAPHVkDemandExecutableStatus;

typedef enum PGRAPHVkDemandExecutableResult {
    PGRAPH_VK_DEMAND_EXECUTABLE_READY,
    PGRAPH_VK_DEMAND_EXECUTABLE_QUEUED,
    PGRAPH_VK_DEMAND_EXECUTABLE_DEFERRED,
    PGRAPH_VK_DEMAND_EXECUTABLE_FAILED,
} PGRAPHVkDemandExecutableResult;

PGRAPHVkDemandExecutableResult pgraph_vk_request_demand_executable(
    PGRAPHState *pg, const PipelineKey *key, uint64_t first_demand_us);
void pgraph_vk_service_demand_executables(PGRAPHState *pg);
```

- [ ] **Step 1: Write a pure state-machine test with callback operations**

Model the same boundary style as `hybrid-prewarm-runtime.c`: the production state machine owns statuses/retries, while tests inject module-ready, request-module, binding-ready, pipeline-ready and submit-pipeline callbacks.

Test these transitions:

```text
missing VSH -> request VSH -> WAITING_FOR_MODULES
missing GS/PSH -> request each exact stage -> WAITING_FOR_MODULES
all modules ready, no binding -> prepare binding -> WAITING_FOR_BINDING or continue
binding ready, no pipeline -> submit once -> PIPELINE_PENDING
pipeline published -> READY
same full key while pending -> deduplicate, update demand counters, no duplicate submit
queue full -> DEFERRED, no blocking callback
permanent stage/pipeline failure -> FAILED_PERMANENT
stale generation -> record discarded
```

- [ ] **Step 2: Run the new red test**

```bash
meson test -C "$BUILD_DIR" --print-errorlogs test-xbox-vk-demand-executable
```

Expected: build failure because the production state machine does not exist.

- [ ] **Step 3: Implement a bounded exact-key table**

Store complete `PipelineKey`, key hash, renderer generation, first/last demand timestamps, demand count, status, attempts and retry time. Use exact byte comparison after hash. Demand requests have priority over speculative family prewarm, but may not evict in-use Vulkan objects or pending demanded work.

When the table is full, return `DEFERRED`, increment a dropped/deferred counter, and preserve the nonblocking contract. Do not invoke the blocking compiler lane.

- [ ] **Step 4: Service requests in stages**

For each retained exact key under the renderer lock:

1. Derive vertex, optional geometry and route-specific fragment module keys from `key.shader_state`.
2. Probe resident modules; request each missing stage through Task 2.
3. Once all stages are resident, create/probe the exact `ShaderBinding` without compiling.
4. Probe the exact complete graphics pipeline.
5. Submit one pipeline build through `request_hybrid_pipeline()` using the ready binding.
6. Let existing generation/ticket-validated publication own Vulkan object insertion.
7. Mark the retained request ready only after the exact pipeline is resident.

Never describe the operation as one queue job: module compilation, renderer-owned module publication, binding creation, pipeline submission and pipeline publication are separate lifecycle boundaries.

- [ ] **Step 5: Service completions without relying on successful flips**

Call `pgraph_vk_service_demand_executables()` after shader completion publication and after pipeline completion publication in `pgraph_vk_process_pending()`. Keep the existing flip-boundary service as an additional opportunity, not the sole path. Add a runtime test where no flip occurs between module completion and exact pipeline readiness.

- [ ] **Step 6: Add telemetry needed by the blackout/UI tasks**

Track at minimum:

```c
uint64_t demanded_executables;
uint64_t deduplicated_demands;
uint64_t deferred_demands;
uint64_t permanent_failures;
uint64_t first_demand_to_ready_us_total;
uint64_t first_demand_to_ready_us_max;
uint32_t pending_demand_executables;
```

Expose a small atomic status snapshot; do not expose mutable queue internals to the UI thread.

- [ ] **Step 7: Run focused worker/runtime tests**

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xbox-vk-demand-executable \
  test-xbox-vk-hybrid-policy \
  test-xbox-vk-hybrid-compiler \
  test-xbox-vk-hybrid-pipeline-builder \
  test-xbox-vk-ubershader-runtime
```

Expected: exact-key deduplication, stage progression, no-flip completion, queue pressure and stale generation tests pass.

- [ ] **Step 8: Commit the demand-executable infrastructure**

```bash
git add \
  hw/xbox/nv2a/pgraph/vk/demand-executable.c \
  hw/xbox/nv2a/pgraph/vk/demand-executable.h \
  hw/xbox/nv2a/pgraph/vk/renderer.h \
  hw/xbox/nv2a/pgraph/vk/shaders.c \
  hw/xbox/nv2a/pgraph/vk/draw.c \
  hw/xbox/nv2a/pgraph/vk/renderer.c \
  hw/xbox/nv2a/pgraph/vk/meson.build \
  tests/unit/test-xbox-vk-demand-executable.c \
  tests/unit/test-xbox-vk-ubershader-runtime.c \
  tests/unit/meson.build
git commit -m "vk: retain demanded executable preparation"
```

### Task 4: Add the safe omitted-draw boundary

**Files:**
- Modify: `hw/xbox/nv2a/pgraph/vk/draw.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/renderer.h`
- Modify: `hw/xbox/nv2a/pgraph/vk/draw-lifecycle.c`
- Modify: `config_spec.yml`
- Modify: `ui/xemu-tweaks.h`
- Modify: `ui/xemu-tweaks.c`
- Modify: `tests/unit/test-xemu-tweaks-config.cc`
- Modify: `tests/unit/test-xbox-vk-draw-lifecycle.c`
- Modify: `tests/unit/test-xbox-vk-ubershader-runtime.c`
- Create: `tests/unit/test-xbox-vk-omitted-draw-state.c`
- Modify: `tests/unit/meson.build`

**Interfaces:**
- Consumes: `pgraph_vk_request_demand_executable()` and typed lifecycle results.
- Produces: the first code path that returns `PGRAPH_VK_DRAW_PREPARE_OMITTED_SHADER_MISS`, plus the dormant/default-Wait configuration contract used by Task 6:

```c
typedef enum XemuVulkanShaderMissPolicy {
    XEMU_VK_SHADER_MISS_WAIT,
    XEMU_VK_SHADER_MISS_CONTINUE_BLACK,
} XemuVulkanShaderMissPolicy;

XemuVulkanShaderMissPolicy xemu_vulkan_shader_miss_policy(void);
```

- [ ] **Step 1: Add the default-Wait configuration contract and its red tests**

Add `vk_shader_miss_policy` to `config_spec.yml` with values `wait` and `continue_black`, defaulting to `wait`. Add the enum/accessor in `xemu-tweaks.{c,h}` without adding a menu row yet. In `test-xemu-tweaks-config.cc`, prove default Wait, explicit persistence, invalid-value fallback to Wait, and independence from `vk_ubershader_mode`.

Run:

```bash
meson test -C "$BUILD_DIR" --print-errorlogs test-xemu-tweaks-config
```

Expected before implementation: the new configuration assertions fail.

- [ ] **Step 2: Add red tests for all draw encodings and repeated omission**

Cover draw arrays, inline elements, inline buffer and inline array. For each, force “no ready route + Continue-black policy” and assert:

- no `vkCmdDraw*` equivalent is recorded;
- no surface generation/dirty publication occurs;
- `pg->draw_time` does not advance;
- the exact demand request appears once despite repeated matching draws;
- `BUFFER_VERTEX_INLINE_STAGING.buffer_offset` does not grow across omissions;
- `num_pending_vertex_ram_reads` is zero after omission;
- private-version stale pages are not committed for an unrecorded draw;
- the next matching draw uses the ready pipeline after publication rather than an older active binding.

- [ ] **Step 3: Make vertex preparation explicitly commit/discard safe**

The audited order is `bind attributes -> prepare_vertex_data() -> begin_pre_draw()`. `prepare_vertex_data()` currently reserves/alters state before pipeline readiness is known. Change that contract:

1. Capture any private remapped bytes into `vertex_version_scratch` before operations that may finish the prior command buffer.
2. Remove `reserve_remapped_attributes()` from `prepare_vertex_data()`.
3. Delay staging alignment/reservation until after `begin_pre_draw()` returns `READY`.
4. Delay marking fixed-mirror pages stale for private/versioned backing until `publish_prepared_vertex_data()` commits the recorded draw.
5. Add `discard_prepared_vertex_data()` that clears pending read bookkeeping and restores temporary descriptor/offset state needed by the next draw; it must not undo safe completed mirror uploads.

This avoids trying to roll back submitted uploads while preventing omitted draws from committing “this draw consumed private vertex data” state.

- [ ] **Step 4: Branch at the uncovered executable boundary before synchronous materialization**

In `create_pipeline()`, after ready candidates and the intended route are known but before `pgraph_vk_activate_shaders()` can materialize missing modules/pipelines:

```c
if (!ready_pipeline && shader_miss_policy_is_continue_black(r)) {
    PipelineKey missing_key;
    pgraph_vk_init_pipeline_key_for_state(
        pg, &requested_state, route, &missing_key);
    PGRAPHVkDemandExecutableResult demand =
        pgraph_vk_request_demand_executable(
            pg, &missing_key, g_get_monotonic_time());
    if (demand != PGRAPH_VK_DEMAND_EXECUTABLE_READY) {
        return PGRAPH_VK_DRAW_PREPARE_OMITTED_SHADER_MISS;
    }
}
```

Use `xemu_vulkan_shader_miss_policy()` from Step 1. Do not add an environment-only or test-only production gate. Do not publish `requested_state` as the active shader/pipeline when omitting. Force the next matching draw through full candidate resolution so it cannot reuse an unrelated old binding after dirty-map clearing.

- [ ] **Step 5: Preserve hard errors and eliminate the misleading log**

Map intentional omission separately in `begin_pre_draw()` and `pgraph_vk_flush_draw_internal()`. Only `FAILED` emits an error. Add perf/trace counters for omitted draw reason, route, exact key hash and demand request result. Queue `DEFERRED` and permanent `FAILED` remain omissions in Continue mode but expose truthful status; they do not call blocking compilation.

- [ ] **Step 6: Verify no stale replay or query/report fabrication**

Do not store vertex/index/uniform data for later replay. The omitted command is consumed. Assert that queries/reports are not advanced as if the GPU executed the draw and that no surface is marked written. Document one-shot render-to-texture/depth/query passes as potentially unrecoverable in this lossy mode.

- [ ] **Step 7: Run focused tests and a product build**

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xemu-tweaks-config \
  test-xbox-vk-draw-lifecycle \
  test-xbox-vk-omitted-draw-state \
  test-xbox-vk-demand-executable \
  test-xbox-vk-ubershader-runtime \
  test-xbox-vk-vertex-version-policy \
  test-xbox-pgraph-vk-reports
ninja -C "$BUILD_DIR" xemu-system-i386
```

Expected: all selected tests and the product target pass; Wait behavior remains unchanged and Continue mode omits rather than blocks for the instrumented shader-miss path.

- [ ] **Step 8: Commit Child PR C**

```bash
git add \
  hw/xbox/nv2a/pgraph/vk/draw.c \
  hw/xbox/nv2a/pgraph/vk/renderer.h \
  hw/xbox/nv2a/pgraph/vk/draw-lifecycle.c \
  config_spec.yml \
  ui/xemu-tweaks.c \
  ui/xemu-tweaks.h \
  tests/unit/test-xemu-tweaks-config.cc \
  tests/unit/test-xbox-vk-draw-lifecycle.c \
  tests/unit/test-xbox-vk-ubershader-runtime.c \
  tests/unit/test-xbox-vk-omitted-draw-state.c \
  tests/unit/meson.build
git commit -m "vk: omit demanded shader-miss draws without blocking"
```

### Task 5: Present black without acquiring the guest framebuffer

**Files:**
- Modify: `hw/xbox/nv2a/pgraph/vk/display-output-state.h`
- Modify: `hw/xbox/nv2a/pgraph/vk/renderer.h`
- Modify: `hw/xbox/nv2a/pgraph/vk/renderer.c`
- Modify: `hw/xbox/nv2a/pgraph/vk/display.c`
- Modify as required after traced call-path verification: `ui/xui/win32-dxgi-present.cc`
- Modify: `tests/unit/test-xbox-vk-display-output-state.c`
- Modify: `tests/unit/test-xemu-win32-dxgi-interop.c`

**Interfaces:**
- Consumes: omitted-draw and pending-demand status from Tasks 3–4.
- Produces:

```c
typedef enum PGRAPHVkBlackoutStatus {
    PGRAPH_VK_BLACKOUT_INACTIVE,
    PGRAPH_VK_BLACKOUT_COMPILING,
    PGRAPH_VK_BLACKOUT_DEFERRED,
    PGRAPH_VK_BLACKOUT_FAILED,
} PGRAPHVkBlackoutStatus;

typedef struct PGRAPHVkBlackoutState {
    uint64_t miss_epoch;
    uint64_t last_omission_frame;
    uint32_t pending_demands;
    bool submitted_draw_after_last_omission;
    PGRAPHVkBlackoutStatus status;
} PGRAPHVkBlackoutState;
```

Expose a read-only atomic snapshot to presentation/UI code.

- [ ] **Step 1: Add state-transition tests**

Assert:

```text
first omission -> COMPILING
new omission while compiling -> same blackout, epoch/last omission advance
all workers complete without a submitted draw -> remain black
submitted draw while demand still pending -> remain black
all demands resolved + submitted draw + next guest flip -> INACTIVE
deferred demand -> DEFERRED
permanent failure -> FAILED and remain black until policy/reset changes
renderer reset/backend switch -> INACTIVE after pending work is cancelled
```

- [ ] **Step 2: Implement a reusable host-black output**

Create the black GL texture/output once during Vulkan display initialization and destroy it during display finalization. Do not modify guest color/depth images. In `pgraph_vk_get_framebuffer_surface()`, read blackout status before taking `pfifo.lock`, setting `sync_pending`, kicking PFIFO or waiting on `sync_complete`; return the ready black output immediately when active.

- [ ] **Step 3: Verify every presentation transport**

Trace normal SDL/OpenGL presentation, shared external-memory presentation, host-copy fallback and Win32 DXGI interop. If every path consumes the texture returned by `get_framebuffer_surface`, keep the blackout there. If DXGI bypasses it, add a direct clear of its already-owned render target before interop blit; do not request a guest image merely to cover it with black.

Add counters proving blackout frames issue zero valid guest framebuffer sync requests and zero host-copy guest uploads.

- [ ] **Step 4: Define recovery at a guest-frame boundary**

Worker completion alone does not clear the image. Clear blackout only after all currently demanded work is resolved, at least one non-omitted draw was recorded after the last omission, and the following guest flip is processed. A new omission cancels pending release. Do not wait for unrelated speculative prewarm work.

- [ ] **Step 5: Run output-state and Windows interop tests**

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xbox-vk-display-output-state \
  test-xemu-win32-dxgi-interop \
  test-xbox-vk-demand-executable \
  test-xbox-vk-omitted-draw-state
```

On the Windows native rig, verify both DXGI enabled and SDL fallback while forcing a multi-second synthetic compile delay. Expected: window/menu remains responsive, game area is black, guest input/audio progresses, and framebuffer sync/upload counters stay flat while black.

- [ ] **Step 6: Commit the presentation portion of Child PR D**

```bash
git add \
  hw/xbox/nv2a/pgraph/vk/display-output-state.h \
  hw/xbox/nv2a/pgraph/vk/renderer.h \
  hw/xbox/nv2a/pgraph/vk/renderer.c \
  hw/xbox/nv2a/pgraph/vk/display.c \
  ui/xui/win32-dxgi-present.cc \
  tests/unit/test-xbox-vk-display-output-state.c \
  tests/unit/test-xemu-win32-dxgi-interop.c
git commit -m "vk: present black during nonblocking shader misses"
```

Only add `win32-dxgi-present.cc` if call-path verification proves it needs a direct path; otherwise leave it untouched and state that both presenters use the common texture result.

### Task 6: Add the explicit Advanced policy and truthful runtime status

**Files:**
- Modify: `ui/xemu-tweaks.h`
- Modify: `ui/xemu-tweaks.c`
- Modify: `ui/xui/main-menu.cc`
- Modify: `tests/unit/test-xemu-tweaks-config.cc`
- Modify: `tests/unit/test-xemu-tweak-paths.py`

**Interfaces:**
- Consumes: the persisted policy from Task 4 plus blackout/demand worker availability and status snapshot.
- Produces:

```c
typedef struct XemuVulkanShaderMissRuntimeState {
    XemuVulkanShaderMissPolicy requested;
    XemuVulkanShaderMissPolicy effective;
    bool available;
    const char *reason;
} XemuVulkanShaderMissRuntimeState;
```

- [ ] **Step 1: Add runtime availability and status tests first**

Extend the Task 4 configuration tests with requested/effective lifecycle cases:

- requested Continue + no renderer -> effective Wait, unavailable;
- requested Continue + OpenGL -> effective Wait, unavailable;
- requested Continue + Vulkan + ubershader mode Off -> effective Wait, unavailable;
- requested Continue + Vulkan + active Fallback/Prewarm/Always + both workers operational -> effective Continue;
- degraded compiler or pipeline builder -> effective Wait with a precise reason;
- active blackout status maps to `Compiling`, `Deferred`, or `Failed` without exposing mutable queue state.

For the first implementation, support Continue only with effective `Fallback`, `Prewarm` or `Always`. `Off + Continue` remains unavailable because current workers are initialized with the hybrid runtime; broadening worker lifetime is a separate proposal.

- [ ] **Step 2: Implement requested/effective status**

Keep the choice live-switchable, but resolve effective policy to Wait whenever Vulkan/hybrid worker prerequisites are absent or degraded. Publish a precise reason rather than claiming Continue is active. Switching back to Wait cancels blackout presentation for future frames but does not reuse or replay an omitted draw.

- [ ] **Step 3: Add the Advanced UI row**

Label: **Shader miss handling**

Choices:

- **Wait — accurate/default**
- **Continue with black frames — experimental**

Help text:

> Avoid waiting for missing Vulkan shaders and pipelines. Game execution, input and audio continue while the game view may turn black. Missing rendering can affect visuals or game behavior, and non-shader stalls can remain. Compiled results are reused when available.

Show Requested and Effective values using the existing Advanced status pattern. Display `Compiling`, `Deferred`, or `Failed` while blackout is active. Do not expose internal hashes or queue records in the normal menu.

- [ ] **Step 4: Run settings/UI path tests**

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xemu-tweaks-config \
  test-xbox-vk-demand-executable \
  test-xbox-vk-display-output-state
python3 tests/unit/test-xemu-tweak-paths.py --cc cc
```

Expected: defaults, persistence, availability and renderer lifecycle transitions pass.

- [ ] **Step 5: Commit the UI/config portion of Child PR D**

```bash
git add \
  ui/xemu-tweaks.c \
  ui/xemu-tweaks.h \
  ui/xui/main-menu.cc \
  tests/unit/test-xemu-tweaks-config.cc \
  tests/unit/test-xemu-tweak-paths.py
git commit -m "ui: add nonblocking shader miss policy"
```

### Task 7: Qualify the lossy mode and preserve the reference path

**Files:**
- Create per child PR after setting `CHILD_PR_NUMBER`: `$EVIDENCE_DIR/README.md`, where `EVIDENCE_DIR=evidence/wiki-xiso-per-test/pr-${CHILD_PR_NUMBER}/issue-198`
- Create per run: machine-readable manifest, raw interval table, shader-demand timeline and final-image captures under the same directory
- Update: child PR descriptions with exact source/tree/executable hashes and measured results
- Update: issue #198 with durable conclusions, not raw speculation

**Interfaces:**
- Consumes: exact optimized product artifacts from Child PR D and current main.
- Produces: acceptance evidence; no new runtime behavior.

- [ ] **Step 1: Build exact optimized artifacts**

Record source SHA, Git tree, build command/options, compiler version, LTO/ISA, executable SHA-256 and matching symbol package for:

1. contemporary `main`;
2. candidate with `Shader miss handling = Wait`;
3. candidate with `Continue with black frames`.

- [ ] **Step 2: Prepare controlled profiles/cache seeds**

Use private profile/cache directories. Prepare separate seeds for empty application caches, learned history + SPIR-V, restored application pipeline cache, and a warm repeat. Record driver version and state whether the driver-internal cache is controlled or unknown.

- [ ] **Step 3: Run deterministic Batman Begins and Azurik transitions**

For each title capture first boot/entry, level restart, and a transition introducing new states. Run matched bracketed orders such as `main / Continue / Wait / Wait / Continue / main`, then reverse on the next repeat. Do not use snapshots alone for the cold path.

- [ ] **Step 4: Record responsiveness and omission metrics**

At minimum:

```text
foreground shader/pipeline wait us
omitted draws by route/reason
blackout count, total duration, longest duration
pending/deferred/failed demand records
first demand -> module ready -> pipeline ready timeline
guest display-write progress
input/audio progress markers
host presentation cadence
framebuffer sync requests and host-copy upload bytes while black
later same-session demand hits
next-launch cache reuse
```

A smooth black window is not rendered FPS. State separately whether guest progress continued.

- [ ] **Step 5: Inspect recovery and persistent damage**

Capture the first visible frame after each blackout and later stable scenes. Record missing render-to-texture content, depth/stencil effects, report/query behavior and gameplay differences. Do not hide permanent effects from the report. If recovery requires replaying stale commands, stop and classify the title/state unsupported for this MVP.

- [ ] **Step 6: Run correctness and regression controls**

Run maintained XISO with candidate Wait and contemporary main; retain expected shared non-passes. Run PGR2 and Morrowind controls. Candidate Wait must preserve output and must not show a repeatable greater-than-2% regression in accepted comparable metrics. Continue mode may differ during/after omission, but all omissions and consequences must be disclosed.

- [ ] **Step 7: Verify lifecycle stress**

Test clean shutdown/writeback, interrupted exit, renderer recreation, backend switch, queue saturation, cache pressure, permanent compile failure and shutdown with pending work. Confirm no worker/process remains and no stale completion publishes into a new renderer generation.

- [ ] **Step 8: Make the decision explicit**

Mark the feature ready only when it demonstrably removes the targeted foreground waits, preserves forward progress while black, publishes/caches exact executables, and has understandable recovery limits. Keep #198 open for residual accurate-path or non-shader causes even if the optional lossy mode is accepted.

## Stop Conditions

Stop the current child PR and open a narrower architectural discussion when any of these occurs:

- Vertex or geometry worker publication requires reading mutable guest state after the request was queued instead of immutable owned data.
- The omitted-draw path cannot prevent private vertex stale-page or staging-state commitment without replaying or rolling back already-submitted GPU work.
- Host blackout still calls `pgraph_vk_get_framebuffer_surface()` far enough to set `sync_pending` and wait for `sync_complete`.
- Queue saturation or unsupported stage work invokes the blocking compiler lane in Continue mode.
- Correct recovery requires replaying skipped draw commands, restoring old guest registers, or preserving unbounded vertex/index/uniform snapshots.
- A proposed fix begins suppressing texture uploads, surface readbacks, reports or guest-visible synchronization unrelated to a shader/executable miss.
- The change requires global serialization around `VkPipelineCache` without measurements proving a driver/cache concurrency fault.
- Three independent implementation attempts fail at different renderer boundaries; reassess the architecture before a fourth patch.

## Non-Goals

- No stale draw replay system.
- No generic guest command recorder.
- No promise of correct pixels or game state while Continue mode omits rendering.
- No default switch away from Wait.
- No automatic renderer/backend change.
- No removal of existing cache validation, generation/ticket checks, descriptor visibility, staging flushes or surface/query semantics.
- No claim that ubershader or blackout handling removes non-shader loading stalls.
