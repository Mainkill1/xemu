# Vulkan Shader Worker Priority Addendum

> **For the assigned implementation agent:** Read this immediately after `docs/superpowers/plans/2026-09-24-vulkan-shader-miss-blackout.md`. This addendum extends Task 2 and Task 7 of that plan and is mandatory.

## Owner requirement

The off-thread shader/module and graphics-pipeline builders are the primary miss-recovery mechanism. They must not take host scheduling time from guest-critical CPU, PFIFO/PGRAPH, APU, input, or presentation work. Demand work has priority over speculative prewarm inside the bounded queues, but both background worker classes should run at a lower host scheduling priority than normal emulator threads.

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

Call the callback exactly once at worker startup for:

- the asynchronous hybrid shader compiler worker;
- the graphics-pipeline builder worker.

Do not lower the compiler's existing blocking/reference worker in this first implementation. Continue mode is forbidden from using that lane. Lowering it could make the accuracy-preserving Wait path block longer and is a separate measured decision.

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

1. the asynchronous compiler invokes the injected priority callback exactly once;
2. the pipeline builder invokes it exactly once;
3. the blocking compiler worker invokes it zero times;
4. `APPLIED`, `UNSUPPORTED`, and `FAILED` all preserve queue processing and clean shutdown;
5. callback failure does not submit work to the blocking lane;
6. repeated jobs do not repeat priority setup or log spam;
7. worker result/status remains readable after startup without exposing mutable queue state.

Focused command:

```bash
meson test -C "$BUILD_DIR" --print-errorlogs \
  test-xbox-vk-background-worker-priority \
  test-xbox-vk-hybrid-compiler \
  test-xbox-vk-hybrid-pipeline-builder
```

## Required telemetry and qualification

Record separately for compiler and pipeline workers:

```text
priority_result = applied | unsupported | failed
jobs_submitted / started / completed / failed
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
- reduced priority causes repeated missed readiness deadlines that erase the foreground-stall benefit;
- a proposed fix uses affinity pinning as a substitute without a separate measured topology design.
