# Relative NV2A flip probe implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task by task.

**Goal:** Repeat the Windows/Deck POC with a relative guest increment target, a completed PFIFO flip hold, and separately identified refresh/presentation events.

**Architecture:** Base the experiment on fork main `ee5ce48b48784f999af374c1452003f8b2b1230f`, preserving its PGRAPH control locking and frame logs. A diagnostic gate owned by `pfifo.lock` holds the command stream at a completed flip stall and schedules a main-loop bottom half to pause the CPU. QMP query/arm/disarm operations expose immutable arm/stop snapshots and current counters. UI presentation records a generation only when framebuffer acquisition began after the diagnostic CPU pause and PFIFO hold.

**Tech stack:** C, QEMU QAPI/QMP and bottom halves, existing NV2A PFIFO/render sync, SDL presentation, existing tester QMP recipes.

**Spec:** User review of the absolute-count POC. This is an experiment, with no universal game-state or exact hardware scanout claim.

1. Add and test the production relative gate: positive bounded deltas, overflow rejection, per-arm generation, target qualification only at completed flip stalls, cancel/re-arm, release and reset.
2. Add diagnostic state/counters and QMP operations; count READ_3D, completed stalls, PCRTC vblank callbacks and completed host presentation calls separately. No per-frame diagnostic file I/O.
3. Connect the gate to PFIFO and schedule CPU pause after device locks are released. Keep pending renderer synchronization runnable while guest methods are held. Cancel on reset/save/load/shutdown and release on resume.
4. Mark UI acquisition/presentation generations; expose arm, quiesce and paused snapshots with monotonic timestamps and observed target overshoot. Preserve existing frame/flip logs.
5. Build both native binaries with CI, author matched tester definitions that settle and capture the parked scene, then arm a relative diagnostic delta. Keep the normal 30-second stationary benchmark and its minimum sample requirement separate from this fixed-work POC.
6. Capture query summaries and images on both rigs, test disarm/resume and a second arm, confirm final idle state, and publish a concrete POC report with limitations.

Review focus: a reached READ_3D count must not imply a completed stall; held PFIFO must still service renderer sync; a stale bottom half must not pause a re-armed request; reset must invalidate the generation; an image acquired before the pause must not receive the stopped generation.

Final review corrections: revalidate the generation and VM state after nested
work inside vm_stop; count only completed SDL/DXGI presentation routes; cancel
active requests before saves. While the experimental probe is enabled, saving
a non-running VM is rejected until resume, including retries after disarm. This
avoids the inherited paused-save ownership problem without changing the normal
probe-disabled snapshot path. Focused tests exercise the actual callback, nested
disarm/resume/re-arm, repeated save attempts, and failed presentation routes.
