# PGRAPH/PTIMER contention implementation plan

**Goal:** Reproduce issue1124's renderer wait → BQL delay → late PTIMER callback → rejected recurring guest target with events and virtual time, then attack flip CAS and FIFO wake ownership.

**Architecture:** Link the unmodified production PGRAPH MMIO, PFIFO wake/stall/worker, PTIMER callback and timer core into a native fixture. Test adapters expose static method/stall boundaries and intercept lock/CAS entry; only renderer service, virtual clock/queue, BQL identity and unrelated device dependencies are synthetic. No guest clock suspension, timer arithmetic changes, interrupt forcing or latency tolerance changes.

**Spec:** User's revised next step, 2026-10-01. Execute inline; research and draft publication authorized, no questions or merges.

## Tasks

- [ ] Register `tests/xbox/ptimer/test-pgraph-ptimer-contention.c` and minimal test adapters in Meson. Use real callbacks, real locks/events, 12,222,222 RDTSC ticks per period, 10,000 periods. Capture first-event failure against the pre-flip parent and success on458730bf53.
- [ ] Drive five CAS interleavings with paused pre-CAS loads and independently calculated words: increment/increment, increment/field, field/field, full-word/increment, full-word/field. Run thousands of controlled operations and count retries.
- [ ] Drive seven PFIFO wake scenarios against production predicate/worker/wait path, including renderer exclusion, held PFIFO, coalesced kicks and check-to-wait boundary. Preserve failures if PFIFO contention independently breaks the timer budget.
- [ ] Run existing focused and unit suites; compile with warnings enabled. Review all results and preserve source/executable/input hashes and exact commands.
- [ ] Execute short Windows cold-start Vulkan and OpenGL smoke plans through the maintained HTTP runner if available; retain missing capabilities/storage failures without policy bypass. Keep useful procedure/tool changes, draft PRs for any XISO suite additions.
- [ ] Prepare a reduced fix plus reproducer against current upstream master after checking existing PRs. Publish only draft PRs with quick before/after tables, all exclusions, remaining gates, and AI/model declaration. Evidence belongs to xemu.

## Review focus

Timer callback is consumed before dispatch; alarm write count includes initial arm plus rearm after every callback. Fake RDTSC and PTIMER mappings are explicitly related without treating their tick units as interchangeable. Full-word replacement must invalidate a pending old CAS snapshot. PFIFO check-to-wait must use actual mutex atomic release/wait. Timing measurements are diagnostic; state/order assertions decide correctness.

## Steering and results

User narrowed the objective: the correctness fix is known; pursue additional measured performance benefit or close the investigation. Fork issue266 was already CLOSED and PR271 MERGED. No additional runtime candidate is justified. Preserve reusable tests in a separate draft for later integration; no upstream correctness-only fix PR.

The historical2b21ca0126 control completes one callback and rejects the next target on its first renderer wait; the current hard gate aborts as expected. Fixed main completes10,000 callbacks and10,001 alarm writes,10,000 forced stale-CAS retries, and1,792 controlled FIFO wake cases. Xbox suite41subtests PASS. A broad automatic rebuild failed on exhausted local /tmp; targeted builds with TMPDIR on disk and the complete built Xbox suite passed.

Retained unhooked production profiling/MMIO cost: 30 balanced paired process-CPU observations ×100,000 read/write pairs, pinned CPU31. Final-v2 median additional cost 38.62006ns/pair, observed range 38.40226–39.17697; hypothetical removal at 60 pairs/sec saves 2.3172µs CPU/sec. The other retained invocation measures 41.12173ns/pair. See the evidence packet's cost receipts. The earlier 38.32436ns exploratory aggregate is excluded because its raw output was overwritten before the preservation guard was added. No game speedup, contended-wait bound, runtime change or performance PR is proposed.

The reusable short Vulkan definition was registered on the HTTP tester; submission failed free-space preflight (1,001,476,096bytes available versus1,073,741,824 required), before guest execution. OpenGL was not started after this shared storage blocker. No gate waiver, SSH bypass or endurance run.
