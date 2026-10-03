# Deck runner update and retained #167 test qualification

## Decision

Runner update is deployed and its retained process test passes. **xemu PR #301 remains draft/HOLD.** This maintenance does not resolve between-process A/A variation or demonstrate an emulator speedup. The original FIFO runtime remains `5679cce0`; the circular experiment remains rejected.

## Quick results

| Check | Before | After | Result |
|---|---|---|---|
| Later scene capture timeout/disconnect | Completed mismatch deleted | Byte-identical mismatch saved | Regression reproduced then fixed |
| Main-based RunnerChecks | RED: 1 failure | 47 PASS / 0 failures | Includes cancellation and first-capture failure |
| Combined maintained runner | 51 checks before regression additions | 53 PASS / 0 failures | Process and live-scene guards compose |
| AgentChecks / XisoChecks | N/A | 142 / 26 passed | Run after upstream #100–103; before narrowly scoped preservation fix |
| Python client checks | Inherited SSH markers reject local fixtures | 92 pass with markers unset only in local fixture process | Production LAN/SSH guard unchanged |
| Configuration + managed metadata | 3,710 files backed up | All 3,710 hashes identical | No saved definition rewrite |
| Historical #167 A/A plan + full report | Previous runner | Parsed documents identical | All original measurements/failures retained |
| Retained FIFO helper | Same executable and immutable test revision | 14/14 PASS, 21/21 artifacts collected | No new performance comparison |

## Source and deployment

Previous Deck runner: `0e05af4a240576638df4ed6611439273f6cdf584`.
New combined source: `e17919c849b840d43171a615b288f559fbec9e4c`, maintained [source draft #89](https://github.com/Mainkill1/Xemu-Test-Runner/pull/89).
Main `08a14f57ddf7691360ed3542ae8955d41cb42215` fixes #100–103 were incorporated with their original behavior: live scene gating, mismatch preservation at scene deadline, application config upload and timeout-test stabilization. Standalone composition assertions now accept the new live gate while retaining process restrictions.

Review found a remaining upstream evidence bug: a completed mismatch was deleted if the *next capture* failed independently of the scene deadline. [Source draft #104](https://github.com/Mainkill1/Xemu-Test-Runner/pull/104), head `88db1e935e7b0069d96489bf646e7e835dfbfe8c`, reproduces and fixes that failure. It preserves evidence before cleanup and rethrows the exact original failure; no retry, relaxed scene tolerance or successful checkpoint is introduced. Cancellation preserves completed evidence. First-capture failure creates no image. Documentation now describes `wait_for_scene` immediately before every segment and the matching result-path check. Read-only review found no blocking defect. The preservation detail in Exception.Data is not printed by current exception text logging; the retained image is a normal result artifact. An unwritable destination can still prevent publication and does not turn failure into success.

Self-contained Linux package SHA-256: `678eded451277d40be1dc061e06740b458f05fa9fb7c7fbb0c403bd5f4e94465`, 33,714,647 bytes. All 59 payload files and exact executable version were checked before activation. Existing native gamepad helper/mapping are reused unchanged. Initial package at `db07d81` is archived and **was not deployed** because review found the evidence bug.

Operator upgrade uses the existing service/config/workspace, only while idle with no pending/testing jobs. All pending managed JSON plus runner config are backed up independently. New unit `xemu-research-runner-20261003-e17919c.service` is active; old application and backup remain. Direct LAN HTTP confirms the exact new revision. Routine selection, start, wait, report and collection used HTTP from the agent host. No firewall, power, cache or driver policy was changed.

## Native reusable check

- Application: `i167-fifo-trace-helper-8b5e-001`.
- Executable SHA-256: `600f77f91085f84c196678a74f8b7143f40152447a8e3346294d9fc160a5aed4`.
- Saved test: `i167-compact-fifo-reference-v1` @ `353c488b6c2ee752c550b32e30a68b561bf9b32ed72579c14346d3038c8a9136`.
- New upgrade-validation request: `i167-runner-e179-retained-20261003-t001`.
- Run: `20261003-140414716-cac8ed764bff468a98ef6192ef6c6230`.
- Canonical result: execution completed, correctness passed, evidence complete, comparison eligible. All 14 TAP cases pass; independent 20,000-operation trace reports 3,256 hits, 6,746 misses, 2,848 middle promotions and 4,461 evictions. This is helper correctness, not real VM/game qualification or timing evidence.

Old campaign `i167-ring-aa-20261003-001` and the saved helper definition are identical before/after. Old post-run-only game definitions remain readable and unchanged, but future execution must satisfy the stronger live scene gate. No observed screenshot was automatically pinned as a reference.

## Preserved failures and boundaries

Initial upstream integration produced one stale test matcher failure; corrected assertion is now retained. First Python local fixture invocation preserved SSH environment and failed 51 assertions/two errors as designed by the loopback guard; the fixture-only invocation passed after unsetting SSH markers. Main-based regression RED demonstrates the deleted frame; both GREEN runs remain archived. An attempted cherry-pick lacked committer identity; continuation used explicit Codex identity. A report-read call omitted the format argument; it failed before mutation and the same archived report was read correctly. Neither was a measurement rerun. No failed attempt or unfavorable sample was excluded.

**Performance: N/A for this runner maintenance.** Previous xemu timing tables remain in the owning PR. Remaining #167 work is to characterize A/A variation, then evaluate above-capacity lookup/fill cost before broader correctness, OpenGL, resource and reached-game checks. Host CPU policy/frequency, thread placement and ASLR effects are still unclassified; this update does not claim to control them.

[Evidence ZIP](EVIDENCE.zip) and [SHA-256 index](INDEX.json) contain tool test outputs, operator proof, unchanged-document snapshots and 20 native result artifacts. All 21 native artifacts were collected locally; the native diagnostics ZIP and binary payloads remain local. Firmware and disk bytes are not included.
