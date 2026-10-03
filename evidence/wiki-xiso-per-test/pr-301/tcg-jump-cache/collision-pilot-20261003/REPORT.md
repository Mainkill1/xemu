# Deliberate collision fixture: first native diagnostic

Recommendation: HOLD. This is fixture qualification on the frozen production parent, not a candidate speedup comparison. All five selected guest leaves execute successfully; all four new 8M-call workloads match independently calculated checksums for every profiled iteration. Mesa private-disk cache qualification passes. The maintained runner correctly reports failed correctness and ineligible comparison because the historical reference has no applicable oracle for any of these five IDs. That failure is retained, not waived or retried.

## What ran and what passed

| CPU fixture | Parent mean work time | Guest outcome | Execution checksum | Paired Improvement % |
|---|---:|---|---|---|
| direct_loop | 0.020176 s | PASS | N/A: existing control | Not measured |
| jump_cache_collision10 | 0.799633 s | PASS | 9d8149e6 | Not measured |
| jump_cache_collision2 | 0.718817 s | PASS | 4f71ed44 | Not measured |
| jump_cache_collision8 | 0.777798 s | PASS | 52f08fda | Not measured |
| jump_cache_noncollision8 | 0.376748 s | PASS | 52f08fda | Not measured |

Times are the mean of ten original guest samples in one attempt; samples and source outputs are retained. New leaves perform 8,000,000 round-robin cdecl calls per sample. DirectLoop is an unchanged existing control with a different work budget. Compare baseline/candidate only with matching leaf/workload, not times between different budgets. The eight-target collision/noncollision pair has identical arithmetic/checksum, but this one parent diagnostic does not measure recovery or establish optimization benefit.

The guest asserts page alignment, target-PC/hash geometry, every iteration's recurrence, immutable page bytes and allocation release. No assertion failed. Its PrintMsg calls go through NXDK DbgPrint; actual PC lines are absent from the runner's collected stdout/stderr, so independently captured address tracing is not claimed. Host geometry checks cover the actual emitter and four page bases. An ELF32 harness validates actual emitted IA-32 code using its own dispatch loop; NXDK object inspection covers the production call/argument cleanup, 8M bound and sticky mismatch. Host harness success alone does not prove production profiling; this native run exercises that path.

## Identity and complete outcomes

Campaign i167-collision-native-19f252d-001, revision1089f0d78f740afc202fe0ee5109a73abe9da3f2428247824efb4fb7ec313169; run20261003-113913864-a8909268c7414dfbbbdf9807c147be3f. Parent76c23c7d444a6f12c9778bb2c35fab513f6c8056, executableSHA5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b. Deck10.0.0.123, Vulkan/RADV,128MiB; unchanged configSHAaa356a9d..., private HDD/EEPROM. Runner0e05af4. No diagnostic-only observer enabled.

Maintained LAN HTTP registered the actual immutable ISO87296b1b35e9f3ade67661ed53d42e9d11ff786bf9ecb07182149c9924c7767f, embedded catalogsha256:5f579ca71a19d22c66ffa213aaa665983125bacb9c962bfd690798b7e3770012, suite revision223112c13d271c453e2ff7f2745180f833f1d119f36b0d191ccdccf7ce6aadb4. Source19f252d; later commits change docs/host tools only, with unchanged src/resources/plans. Suite remains unverified. Zero warmups, multiplier1, per_iteration, monolithic explicit five-leaf selection, ConfigurationSource=suite. Frozen plan inspected before start.

Execution completed, guest plan receipt and exact5-leaf coverage pass, evidence complete, cache qualified, correctness failed solely xiso_oracle_coverage: no pinned oracle for5leaves; comparison ineligible. All309 indexed artifacts collected, zero exclusions, guest extraction SHA checked. Raw missing-reference result is preserved. The unpaired campaign report also correctly reports absent eight-attempt ABBA/BAAB schedule; it supplies no comparison rows. Initial two local CLI usage mistakes and an unsubmitted reference-template hash-key correction are retained and launched no workloads. No reference replacement, uncontrolled cache waiver, cache purge or workload rerun.

## Retained source and next bounded qualification

Source draft https://github.com/Mainkill1/xemu-perf-tests/pull/54 contains the actual four fixtures, catalog mappings/plans, executable host contracts and documentation. Initial145 contracts pass; the later source-reference tool adds one meaningful contract, for146passed. RED missing records is captured, then GREEN. Wrong arithmetic and noncollision geometry mutations fail before restoration. Release XISO builds; warnings in unchanged source/dependencies remain disclosed, no modified-code warning observed. No evidence-only commit is made in the suite repository.

A source-derived arithmetic-only reference now computes each result via independent Python recurrence and the page's specified IA-32 bytes. It accepts no measured input, includes no framebuffer hash or measured timing, and never overwrites a pinned file. This supports a separate CPU-only suite with four new leaves and equal-work noncollision control; it does not qualify rendering or waive the original DirectLoop failure. Original suites/references stay immutable. Bounded comparisons require source/reference review, fresh A/A, then physical ABBA/BAAB against matched builds. Rendering, physical Xbox, VM remapping, spanning pages, reset/load, stale TB storage, retail/cross-renderer performance and resource checks remain separate gates. No readiness or issue closure is justified.

## Evidence

EVIDENCE.zip retains public source/build/contract and native input/outcome/measurement logs. INDEX.json inventories all retained files; private package/firmware/runtime state and redundant diagnostics ZIP are manifest-only. No measured sample or failed outcome is excluded. Emulator evidence belongs to xemu draft301.
