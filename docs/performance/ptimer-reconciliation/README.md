# NV2A PTIMER upstream reconciliation

## Goal and reference

Restore the earlier fork PTIMER correctness behavior while resolving the
upstream changes around it. This is a reconciliation of existing work, not a
new clock model, a global QEMU timer rewrite, or a blanket upstream revert.

Owner request and investigation: [issue #266](https://github.com/Mainkill1/xemu/issues/266).
The owner reports the earlier fix worked correctly; preserve that behavior
and qualify this new integration separately.

- Integration parent: `2cbabc7152866dd19fb2c6279c776019f3f5df9a` (accepted main).
- Earlier semantic repair: [PR #59](https://github.com/Mainkill1/xemu/pull/59),
  source `0043629b0bc13d2b34a1bf3c1008171ad8eecb8f` (runtime PTIMER blob
  `deac273f2dd5dce05f30a657f3ccd3ead79efaec`).
- Subsequent scheduling work: [PR #81](https://github.com/Mainkill1/xemu/pull/81),
  source `98f1a7a49cb7a6ccb8feba20438dc86f9ead58d5`.
- Upstream rounding/masking: [upstream #3035](https://github.com/xemu-project/xemu/pull/3035),
  commit `75650bd8cd91945f7b79774e2cee0b200ca373ff`, integrated through
  [PR #120](https://github.com/Mainkill1/xemu/pull/120).
- Reported failing build: `cca8e92398965b2ed75d6ce5606c9d099a23047d`.
  Its PTIMER blob and the integration parent's are both
  `e25c936ff3976f481f483df9ea2a5876ef53edc0`.

## Required combined behavior

1. Retain the fork's overdue multi-epoch catch-up, raw zero-ratio register
   handling, pending-state reconciliation, and restored virtual-time basis.
2. Restore explicit guest `alarm_armed`, including a valid target of zero
   across full-counter wrap. Masking/canceling a host timer does not disarm
   the guest comparator.
3. Reconcile elapsed alarms under the old state before W1C acknowledgment,
   ALARM replacement, mask/rate/TIME changes, and NVPLL changes. Acknowledgment
   must not materialize an already-cleared occurrence on later unmask.
4. Retain upward-rounded future scheduling, but use the earlier phase-aware
   inversion of both forward quantizations at one virtual-clock sample.
   Preserve checked wide arithmetic, source-counter wrap and signed deadline
   saturation. Do not use a second sample to shift the derived deadline.
5. Retain upstream suppression of masked host callbacks. Stopped clocks keep
   the explicit guest alarm without requiring a dummy callback. Ordinary ACK
   must preserve an unchanged valid host schedule rather than always requeue.
6. Preserve current unrelated renderer, input, dependency and Windows-wait
   changes. Do not add an anchored clock, host thread, timing override,
   forced interrupt, title patch, or release-default `-icount` workaround.

## File-level implementation plan

- `hw/xbox/nv2a/ptimer.c`: reconcile the earlier semantic repair with current
  scheduling, retain the no-op ACK path, and adapt version-aware restoration.
- `hw/xbox/nv2a/nv2a_int.h`: restore explicit armed state and declarations.
- `hw/xbox/nv2a/nv2a.c`: reset absent state before load, pass the stream version,
  and append/version the explicit field without changing legacy field order.
- `hw/xbox/nv2a/pramdac.c`: route source-clock changes through PTIMER's existing
  reconciliation operation while retaining the raw programmed coefficients.
- `tests/unit/test-xbox-nv2a-ptimer.c`: retain existing behavioral controls and
  add masked ACK, zero target, phase, clock transition and restoration cases.
- `tests/xbox/ptimer/`: keep the upstream suite compatible with the real state
  layout and production arithmetic. Do not delete an inconvenient test.

Development order: reproduce the current failures first; restore the original
behavior; add integration-specific recovery tests; run focused checks and
negative controls; inspect the final diff; then qualify the exact product build.

## Snapshot contract

The current main uses NV2A stream version 4. Keep its alarm/time/timer fields
in the same order and append the explicit armed field in a new version.
Pre-v4 streams omit alarm state, so clear old live state before deserialization.

**Do not copy the old v4 reader unchanged.** Pre-integration v4 represented
arming through its queued timer. Post-#120 v4 can contain an armed nonzero
alarm with no queued callback because the interrupt was masked. Recover
supported v4 armed state from `timer_pending() || alarm_time != 0`, then
reconstruct the host deadline from the restored guest state. New streams
preserve `alarm_armed` directly, including zero-valued targets. Already
ambiguous zero-target/no-timer v4 state cannot be recovered exactly.

Test actual old/new streams before merge. Calling the post-load function with
fixture state is useful unit coverage, not proof of full migration compatibility.

## Regression and acceptance gates

The current-source standalone probe was rerun on September 29, 2026: six
controls pass and three fail as expected (masked ACK/unmask, the 9 ns phase
case, and a wrapped zero alarm). That is source-path evidence with simulated
clock/timer/IRQ infrastructure, not a Windows or full-emulator result.

Required regression cases:

- At 233,333,333 Hz, first-tick deadlines at ratios 1/1 and 1/2 remain 5 ns.
- At 100 MHz, ratio 1/1, a one-tick alarm programmed at 9 ns is due at 10 ns,
  not 19 ns; generated phase/ratio cases match the forward-clock oracle.
- A masked alarm expires, is acknowledged without a preceding status read,
  and is unmasked: the old occurrence stays cleared.
- Programming ALARM=0 immediately before full-counter wrap remains armed.
- Late callbacks skip multiple epochs directly; ordinary ACK retains the
  valid future deadline; masked polling never queues a callback just to poll.
- Zero ratio/source-clock stop and restart, rate/TIME/NVPLL changes, pending
  preservation, reset, signed horizon and supported restoration variants.

Before merge: compile the exact Windows product; run upstream/fork/generic
PTIMER suites plus real timer-list/IRQ and VMState coverage; run repeated
normal-clock Def Jam cold starts and sustained matches on OpenGL and Vulkan;
compare PGR2/Morrowind pacing and current XISO correctness. Retain exact
source/binary/settings identities and adverse results with this emulator PR.
Do not submit evidence-only PRs to the test-suite repository.

Historical tests establish the reference behavior, not a pass for the new
integration. Keep the PR draft until its unexecuted qualification is complete.
