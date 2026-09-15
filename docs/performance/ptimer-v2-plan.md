# PTIMER scheduling replacement plan (issue #73)

Base: `9148241de690617ac0a21a26b41858585c1e3cab` (`main`, 2026-09-12). The fixed cycle baseline remains `9f618d6d8c4c446ef023955f3d4de22f661f61a4`.

This draft PR covers the first independently measurable part of issue #73: reconstruct the corrected guest alarm semantics and phase-aware deadlines from archived PR #59 on current main, then separate desired deadlines from host queue side effects and avoid queue work when the schedule is unchanged. The archived PR #59 performance hold is not inherited as a pass. Pending-alarm callback suppression, earlier-only rearming, anchored guest clock, and Windows host-wait changes remain separate experiments.

1. Restore and adapt the production PTIMER fixture from PR #59, including negative controls for truncating deadlines, masked alarms, migration, clock changes, and wrap. Run it against the old code and corrected implementation.
2. Reconstruct explicit `alarm_armed`, exact phase-aware deadline arithmetic, pre-ACK reconciliation, clock-change handling, and versioned snapshot state. Keep existing register and IRQ behavior where not deliberately corrected.
3. Add a deterministic queue-decision core and PTIMER-owned host schedule state. Calculate the desired virtual deadline separately. Test identical ACKs, masked writes, callback consumption, invalidation, and restore so redundant timer queue mutations disappear without losing alarms.
4. Build the exact new head and run the focused production translation-unit tests, timer-queue integration checks, snapshot checks, and a short diagnostic comparison against previous main. Keep the PR draft while full XISO, PGR2 full/snapshot, and Morrowind snapshot qualification remain pending.
5. Publish the draft branch and PR on `Mainkill1/xemu`, with parent/head hashes, behavior and performance tables, completed tests, explicit hold gates, and links to issue #73. Publish run artifacts in `Mainkill1/xemu-perf-tests` when generated.

Do not alter `baseline`, `main`, unrelated controls, global clock policy, or Windows waiting as part of this PR.
