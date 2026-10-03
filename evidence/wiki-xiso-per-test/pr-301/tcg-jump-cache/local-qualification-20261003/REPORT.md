# Eight-entry cache: source and local qualification

Decision: HOLD. This packet qualifies the local implementation for bounded native experiments; it contains no candidate performance gain.

Parent: `76c23c7d444a6f12c9778bb2c35fab513f6c8056`. Product: `5679cce099eb777977e761245de47e77d05d0c05`. Owning draft: [xemu #301](https://github.com/Mainkill1/xemu/pull/301). Diagnostic motivation remains [#299](https://github.com/Mainkill1/xemu/pull/299), whose recoverability counts are not speedups.

## Results

| Check | Result | Limit |
|---|---|---|
| GCC14/O2 i386-softmmu emulator | OFF and ON build successfully | Native game behavior pending |
| Actual cache helper | 13/13 checks in both unit builds | Unit macro explicitly enables helper even in OFF emulator build |
| Delayed publication | 10,000 controlled owner/foreign-writer periods | Writer completes before fill; no hook inside fill/lookup |
| Four deliberately broken variants | All abort in their expected retained tests | Targeted negative controls, not complete correctness proof |
| CPUJumpCache size | Parent/OFF 65,552 B; ON 65,712 B | +160 B per CPU |
| Primary array | Offset 16 B, size 65,536 B in all builds | Unchanged 4,096-entry layout |
| Default-off symbols | No victim helper; no outlined tb_lookup | Assembly evidence only |
| Default-off dispatch | Same instruction shape; source-line values and logging-string relocation differ | Runtime parity pending |
| Full unit rebuild | FAIL: unchanged resampler missing math.h under GCC14 | Full suite is not passing |
| Fresh code review | No concrete safety defect; bounded native experiment allowed | HOLD, no readiness/merge authorization |

The original direct-map collision control deliberately aborts at recovery of a colliding entry. The production helper then passes that retained collision test and full-key, CF_INVALID, FIFO, noncollision, alias, full/page/spanning/targeted-clear, overlapping invalidator, delayed-fill and guarded-storage-reuse cases. Mutation controls remove cflags checking, generation-history reset, active-writer checking or postpublication rejection; each is rejected by its specific retained test.

## Preserved failures and corrections

- First enabled link omitted the module from libsystem because the custom host-config source-set condition did not select it. The existing option-selected TCG source pattern corrected this; both builds pass.
- Plain inline let GCC outline enabled tb_lookup. Applying always_inline to both configurations changed default-off register allocation. The final attribute applies only when enabled, and disabled dispatch shape is preserved.
- ninja check-unit is not a target. The actual Meson unit gate attempts rebuilding and fails in the unchanged resampler test.
- The first mutation driver expected a literal assertion-failed phrase, while GLib reported the expected NULL assertion as ERROR/Bail out. Its mutant had correctly aborted. The corrected driver preserves that first transcript and accepts the actual GLib failure format; four final mutations are rejected.

## Remaining qualification

Native helper execution, fresh parent A/A, default-off parity and enabled ABBA/BAAB are separate campaigns on Steam Deck 10.0.0.123 with runner 55a360da. Keep original references and explicitly choose suite configuration; do not waive Mesa qualification. Two CPU fixtures cannot establish retail benefit. Real VM remap/second-page, reset/load, generated-code exhaustion/recycling and writer interleavings inside helper execution remain unqualified. Other hosts and weak-memory native behavior are unqualified.

No baseline/candidate performance table is available for this product yet. Future measurement rows use seconds of fixed guest work, time saved = baseline - candidate, and Improvement % = 100*(baseline-candidate)/baseline; positive means better. A/A variation is reported independently, not subtracted as a correction.

## Evidence

EVIDENCE.zip retains the local logs, TAP transcripts, original failure controls, layout objects' measured sizes, dispatch disassembly/diffs, review summary, immutable package SHA-256 manifest, and reusable verification/mutation scripts. Emulator executables, dependent libraries, transient formatter installation and private guest assets are excluded from this local packet. INDEX.json identifies every archived file by length and local SHA-256. Production tests and helpers are committed directly in this PR.
