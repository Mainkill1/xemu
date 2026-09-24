# Vulkan Shader Worker Priority Addendum

> **For the assigned implementation agent:** Read this immediately after `docs/superpowers/plans/2026-09-24-vulkan-shader-miss-blackout.md`. This addendum extends Task 2 and Task 7 of that plan and is mandatory.

## Owner requirement

The off-thread shader/module and graphics-pipeline builders are the primary miss-recovery mechanism. They must not monopolize host scheduling time needed by guest-critical CPU, PFIFO/PGRAPH, APU, input, or presentation work. Demand work has priority over speculative prewarm inside bounded queues. **Do not hard-code the assumption that every asynchronous compiler must always run below normal priority.** Cemu's current Vulkan compiler deliberately keeps one compile thread at normal priority while lowering the rest so all compilation cannot be starved. Treat worker priority as a measured scheduling policy: speculative/prewarm work should normally be low priority, while demanded work may need a normal-priority lane or promotion when evidence shows lower-priority workers miss readiness deadlines.

This is separate from the blackout policy:

- background construction should reduce foreground waits in both Wait and Continue modes;
- Continue mode may omit a draw rather than waiting when no permitted executable is ready;
- worker priority failure must never cause Continue mode to fall back to foreground compilation.

## Source audit result

`include/qemu/thread.h` exposes thread creation, joining, identity and affinity, but no cross-platform priority setter in the audited fork. Do not invent or call a nonexistent `qemu_thread_set_priority()` API. Add a small Vulkan-local best-effort helper and keep its behavior explicit.

## Required interface

Create:

- `hw/xbox/nv2a/pgraph/vk/background-worker-priority.h`
- `hw/xbox/nv2a/pgraph/vk/background-worker-priority.c`
- `tests/unit/test-xbox-vk-background-worker-priority.c`

Use:

```c
typedef enum PGRAPHVkWorkerPriorityResult {
    PGRAPH_VK_WORKER_PRIORITY_APPLIED,
    PGRAPH_VK_WORKER_PRIORITY_UNSUPPORTED,
    PGRAPH_VK_WORKER_PRIORITY_FAILED,
} PGRAPHVkWorkerPriorityResult;

typedef PGRAPHVkWorkerPriorityResult
(*PGRAPHVkSetWorkerPriorityFunc)(void *opaque);

PGRAPHVkWorkerPriorityResult
pgraph_vk_lower_current_worker_priority(void);
```

Extend `PGRAPHVkHybridCompilerConfig` and `PGRAPHVkHybridPipelineBuilderConfig` with a priority callback and opaque pointer. Tests inject a callback so they can prove call count and failure behavior without changing the test runner's real scheduling.

## Worker policy

The first implementation must expose scheduling as an explicit experiment rather than baking one global rule into the worker type.

Required controls/variants:

1. current/default worker scheduling;
2. all speculative/prewarm workers lowered;
3. one normal-priority **demand** lane plus lower-priority prewarm/speculative workers;
4. promotion of an already queued exact job from prewarm/speculative to demand urgency without duplicating the work.

Cemu reference:
https://github.com/cemu-project/Cemu/blob/5e09ec72a43dc8e857f94619d3b0a40c2bd65298/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.cpp

Cemu's Windows compiler pool keeps thread 0 at normal priority and lowers the rest specifically to avoid starving every compile thread.

Use a job-level urgency classification:

```c
typedef enum PGRAPHVkCompileUrgency {
    PGRAPH_VK_COMPILE_SPECULATIVE,
    PGRAPH_VK_COMPILE_PREWARM,
    PGRAPH_VK_COMPILE_DEMAND,
} PGRAPHVkCompileUrgency;
```

Exact duplicate work must be promoted in place:

```c
if (existing_exact_job) {
    existing_exact_job->urgency =
        MAX(existing_exact_job->urgency, requested_urgency);
    reprioritize(existing_exact_job);
    return PGRAPH_VK_COMPILE_DUPLICATE_PROMOTED;
}
```

The compiler's existing blocking/reference worker remains unchanged. Continue mode is forbidden from using that lane. The dedicated background **demand** lane, if the experiment proves it necessary, is still nonblocking from the renderer's perspective; it is not the reference blocking worker.

Platform behavior:

| Host | Required best-effort operation |
| --- | --- |
| Windows | `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL)` |
| Linux | Increase only the calling worker thread's niceness by `5`. Do not use a fallback that changes the whole xemu process. |
| macOS | `pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0)` |
| Other hosts | Return `PGRAPH_VK_WORKER_PRIORITY_UNSUPPORTED` without changing scheduling. |

A platform branch must be disabled rather than made process-wide when thread-local behavior cannot be established on that host/toolchain.

## Failure behavior

Priority is an optimization contract, not an initialization dependency:

- `APPLIED`: worker continues and publishes the status;
- `UNSUPPORTED`: worker continues, logs once, and publishes the status;
- `FAILED`: worker continues, logs the host error once, and publishes the status;
- none of these results may abort initialization;
- none may redirect work to synchronous compilation;
- Continue mode still obeys bounded queue/deferred/failed behavior.

The normal UI does not need a priority control. Effective shader-miss status may state that background priority was not applied, while detailed platform errors stay in diagnostics.

## Required tests

Add production-linked tests proving:

1. a worker configured for lowered scheduling invokes the injected priority callback exactly once;
2. a normal-priority demand lane does not invoke the lowering callback;
3. the blocking/reference compiler worker invokes it zero times;
4. `APPLIED`, `UNSUPPORTED`, and `FAILED` all preserve queue processing and clean shutdown;
5. callback failure does not submit work to the blocking/reference lane;
6. repeated jobs do not repeat priority setup or log spam;
7. exact queued work can be promoted from prewarm/speculative to demand without duplicate compilation;
8. worker result/status remains readable after startup without exposing mutable queue state.

Focused command:

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xbox-vk-background-worker-priority \
  test-xbox-vk-hybrid-compiler \
  test-xbox-vk-hybrid-pipeline-builder
```

## Required telemetry and qualification

Record separately for each compiler/pipeline worker lane and urgency class:

```text
worker_lane = demand | prewarm | speculative | reference
urgency = demand | prewarm | speculative
priority_result = normal | applied-low | unsupported | failed
jobs_submitted / promoted / started / completed / failed
submitted_us -> started_us runnable delay
started_us -> finished_us worker elapsed time
queue depth and queue-full/byte-limit events
worker CPU usage during the affected transition
foreground shader/pipeline wait removed
guest CPU, PFIFO/PGRAPH and APU progression while work is active
```

A lower-priority worker that never receives enough CPU to finish before first demand is not automatically an improvement. Qualification must compare readiness deadlines, foreground wait, guest progression and total blackout—not only worker CPU usage.

Test at minimum:

- the reported Windows/NVIDIA configuration;
- a host with fewer available cores than the reporter's system;
- a run with simultaneous compiler and pipeline work;
- queue saturation;
- active audio and guest CPU load;
- Wait and Continue modes using identical cache seeds.

## Files added to Child PR B

In addition to the main plan's Task 2 files, Child PR B modifies:

- `hw/xbox/nv2a/pgraph/vk/hybrid-compiler.h`
- `hw/xbox/nv2a/pgraph/vk/hybrid-compiler.c`
- `hw/xbox/nv2a/pgraph/vk/hybrid-pipeline-builder.h`
- `hw/xbox/nv2a/pgraph/vk/hybrid-pipeline-builder.c`
- `hw/xbox/nv2a/pgraph/vk/meson.build`
- `tests/unit/test-xbox-vk-hybrid-pipeline-builder.c`
- `tests/unit/meson.build`

Suggested Child PR B commit subject:

```text
vk: compile demanded shader stages on background workers
```

## Stop conditions

Stop and request architectural review when:

- the only available implementation changes priority for the full emulator process;
- Continue mode needs the normal-priority blocking worker to make progress;
- priority setup requires elevated privileges or a fatal initialization path;
- the helper leaks platform APIs into generic draw or shader policy code;
- an all-lowered configuration causes repeated missed readiness deadlines; in that case test a bounded normal-priority demand lane before abandoning background compilation;
- a proposed fix uses affinity pinning as a substitute without a separate measured topology design.


## Prior-art correction logged after the initial addendum

The detailed prior-art review is now authoritative for scheduling decisions:

- `docs/performance/issue-198-prior-art-implementation-playbook.md`
- #207 owns fully off-thread recipes, urgency, duplicate promotion, and scheduling experiments.

Do not interpret this addendum as permission to start with a large custom scheduler. First collect submitted-to-started delay, readiness margin, guest progress, and worker CPU under the existing implementation. Add the smallest lane/promotion mechanism needed by that evidence.
