# Steam Deck controls for the actual eight-entry candidate

Decision: HOLD. No candidate speedup is measured in this packet. The A/A table compares the identical production-parent executable to itself; differences demonstrate variation. Default-off parity and enabled candidate ABBA/BAAB are separate campaigns.

## Fresh parent A/A

Statistic: median of four attempt-level mean guest work times per label, ten fixed-work iterations per attempt. Physical order A1 B1 B2 A2 B3 A3 A4 B4. Both labels use parent `76c23c7d`, SHA-256 `5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b`.

| Test | Backend | Parent A | Same parent B | Time saved | Apparent Improvement % | ABBA | BAAB | Correctness |
|---|---|---|---|---|---|---|---|---|
| code_stable | Vulkan | 2.515709 s | 2.506263 s | +0.009445 s | +0.38% | +0.54% | +0.27% | 8/8 pass |
| code_rewrite | Vulkan | 2.988703 s | 3.118815 s | -0.130112 s | -4.35% | -4.30% | -1.76% | 8/8 pass |

`code_stable` performs 50 million operations on stable code; `code_rewrite` performs one million code-changing operations. These original fixtures are controls and do not guarantee the deliberate primary collisions used by the retained host test. Time saved = A-B. Improvement % = 100*(A-B)/A; positive is better. A/A differences are not optimization gains, are not subtracted as a correction, and do not define a universal noise threshold. The rewrite variation makes small candidate differences inconclusive. Full per-test min/mean/max timings and all eight sample sets are retained in the runner report and raw extraction records.

Deck `.123`, Vulkan/RADV, 128 MiB, fixed configuration SHA `aa356a9d7b916ace9b71ff1ef47946db1ac289e36a8b00235c4397f57523b0b8`, suite `b45ca3dd...`, original ISO `74a10c40...`, catalog `34b8ee7f...`, warmups 0, multiplier 1, per_iteration. Exact identities are in aa-plan.json. All eight attempts pass correctness/evidence/comparison; each cold private Mesa namespace verifies observed disk writes with no waiver or issues. Suite-wide qualification remains unverified; this evaluates only the two originally pinned CPU leaves.

Runner `55a360daf82a699ecd12d69d8484f32bf1cf3752` requires and records explicit suite configuration. No SSH testing, input intervention, global cache purge, changed expected results or emulator retry occurred in A/A.

## Retained native helper test

Actual production helper and clear routines pass all 13 cases on Deck, including the 10,000 synchronized delayed-fill control. Executable SHA `1f5dc446c2e87f87ccbf4bb2b41dc869629639e374f3ab0c891194f2e600f9b4`. This is a standalone process component check, not game/VM correctness or graphics cache qualification.

Retained saved test: `i167-victim8-production-helper-v2@446055138a1e54b2bb954318f4d7914484f8ff5ad7043e10acf843d4dc6d7b7b`. Build replacement slot is `test-tcg-victim-cache`; immutable libraries and all 13 case assertions remain pinned. Production C tests are committed in owning [draft xemu #301](https://github.com/Mainkill1/xemu/pull/301).

### Preserved qualification failures

1. The first A/A report returned ineligible because B4 was not yet in the comparison index, although all eight attempts were terminal and eligible. Complete collection retained all 2,480 artifacts with zero exclusions. A subsequent read of the same campaign includes all eight, with no workload rerun. Preserve aa-report.initial.* and the original stopped sequence log; the collector now reads the report after collection.
2. Helper attempt `i167-victim8-helper-5679-001` passed all 13 correctness checks but failed evidence: the fixture guessed a minimum 750-byte TAP file, while the complete transcript is 695 bytes locally and on Deck. Preserve the attempt and its 21 artifacts. The corrected fixture requires transcript presence, the exact 13-case plan and every case token. The explicitly disclosed second component attempt `i167-victim8-helper-5679-002` uses the identical executable/source and passes correctness and evidence. Neither attempt is performance evidence.

## Remaining

Default-off parity and actual enabled performance remain pending. Real VM remapping, second-page remapping, SMC/lifecycle/reset/load/code-storage recycling, inside-helper invalidation interleavings, cross-renderer and representative retail comparisons are not qualified by these controls. Source review permits bounded native testing; PR remains HOLD and draft.

## Archive scope

EVIDENCE.zip preserves all control timings, logs, assessments, manifests, reports, initial failures, corrected fixture and reusable workflow scripts. All 2,480 A/A artifacts plus both 21-file component archives were collected locally. INDEX.json includes SHA-256/size for every collected file; private state/driver-cache bytes and redundant diagnostic ZIP payloads are represented by inventory rather than copied into the public ZIP. No measurement, sample or failed outcome is omitted.
