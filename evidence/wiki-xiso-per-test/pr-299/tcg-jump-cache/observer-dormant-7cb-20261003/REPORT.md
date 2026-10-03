# Issue #167: dormant-probe comparison and default-off correction

**Decision: HOLD. The apparent stable-code gain is not a cache optimization.**
The old compile-disabled build outlined `tb_lookup`; the diagnostic build forced
it inline even with collection OFF. This confounded the intended observer-cost
comparison. The branch now restores the parent's original compiled-out lookup
expression. Fresh parent/corrected comparisons have new identities; their results
are not in this packet.

## Matched comparison, with its actual meaning

Both builds use source `7cb38844fb2dddb1b24cc65501831c9cdcedb43e`, GCC 14 O2,
i386-softmmu, identical supporting payloads and runtime mode `off`. A has the
probe compiled out; B has it compiled in with collection inactive. This changes
both hook presence and compiler decisions. It does not isolate accounting cost.

The maintained Deck runner executed physical ABBA followed by BAAB, four attempts
per build and ten guest samples per leaf. Values below are medians of four
attempt-level mean work times. Saved time is A minus B; positive Improvement %
means less time, `100 * (A - B) / A`.

| Fixed work | A: compiled out | B: compiled in, OFF | Time saved | Improvement % | ABBA % | BAAB % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stable code, 50 million operations/sample | 3.347310 s | 2.550077 s | +0.797233 s | +23.82% | +23.83% | +23.72% |
| Code rewrite, 1 million operations/sample | 2.976472 s | 3.122605 s | -0.146133 s | -4.91% | -3.15% | -5.66% |

The runner reports no order disagreements and all eight attempts eligible.
All mean/median/min/max/p95 metrics and individual attempt values are retained.
Stable median-sample/p95 changes are +23.61%/+24.12%; rewrite changes are
-4.97%/-5.30%. Ten samples do not resolve a reliable tail: nearest-rank p95
equals the maximum. Rewrite's -4.91% is inconclusive beside the earlier identical
binary A/A difference of -4.25%; that control does not establish a universal
noise floor or prove the rewrite difference is solely noise.

## What the investigation found and corrected

`nm` and disassembly show an outlined `tb_lookup` in A and no such function in B.
The enabled path has an explicit always-inline declaration. Compiling the actual
parent's `cpu-exec.c` under the same generated OFF configuration/headers/flags
also inlines the lookup. Routing the OFF branch through the new observed-lookup
wrapper changed GCC's decision. This supports the compiler explanation; it does
not isolate how much of the native difference that decision caused.

Source repair `31c083ece2e7d44e87b2c7c4039cfc6b2365848d` restores the parent's
literal atomic load and complete PC/cs_base/flags/cflags predicate when compiled
out. Source review verifies the whole disabled function matches the parent after
comments/whitespace removal, and the enabled function is unchanged. No cache
retention, victim execution, timer or invalidation behavior was added.

The matched GCC14 codegen gate fails on old A and passes on the repaired build.
Both committed OFF and ON emulator builds succeed; all 9 collector, 7 validity
and 13 classifier checks pass in both configurations. Both repaired builds and
a fresh whole-emulator parent build have inlined lookups. Parent/corrected OFF
feature configurations differ only by the new, undefined probe macro. This is
build/source verification, **not native proof of recovered performance**.

The prior full `check-unit` failure in the unchanged resampler remains retained
in the first packet; these 29 passing checks do not make the full suite green.
Two local symbol/config assertions guessed an outlined inline helper, then an
incorrect collector name/macro value; their inspection failures were preserved
and replaced with checks of the actual `new`/`clear` symbols and feature define.
No native attempts were repeated because of those local mistakes.

## Exact identities, state and correctness

- Deck `10.0.0.123`, runner `c264004dfc906eef008c8a7235764c37daee330b`;
  normal operation uses maintained LAN HTTP, with #93/#94/#95 fixes included.
- A SHA-256: `4a85fa3316e1f29ca4526ccad6488ff5ebf0414998c01328132e76a50fbc8de8`.
- B SHA-256: `3cfe630b1e210a95bf48aa3df3141ff5cc9ab024112242b4262fa4ff3202c272`.
- Campaign `i167-obs-dormant-7cb-001`, revision
  `a80fcc89fc73c12cb3feb0b4c52abdd3706cad842469a21aaa13325bbc742033`.
- Suite revision `9d5f8b7f716852a6fcb863db4d67175e0c770434fa654ec0833746f86c822f42`.
- ISO `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`;
  catalog `sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4`.
- Same pinned `c02a1a44` CPU leaves/oracles, one chunk, Vulkan/RADV, 128 MiB,
  zero warmups, multiplier one and per-iteration completion.

Both leaves passed their original references in all eight attempts. Every attempt
completed with complete evidence and comparison eligibility. Every Mesa namespace
started empty and qualified through private disk writes, with zero state issues
and no waiver or shared purge. Seven runs produced 275 cache files / 2,886,729
bytes; A4 produced 276 / 2,886,974. OS page cache and driver memory cache remain
uncontrolled limits. Suite qualification remains **unverified**.

There were no native failures, missing attempts, reference changes or silent
reruns. No monitor commands, timing/occupancy sampler or operator intervention
was injected. Bulk collection waited for campaign completion and an idle Deck.

## Evidence and continuation

[SUMMARY.json](SUMMARY.json) retains all distributions and cache qualifications.
[INDEX.json](INDEX.json) hashes every native file. [EVIDENCE.zip](EVIDENCE.zip)
holds canonical reports/attempts, raw guest results, host metrics, state receipts,
the frozen contract, compiler evidence, build/check logs and source review.
**2,481 files collected, zero exclusions:** 280 native files are public; 2,201
private runtime/cache payloads remain locally retained and individually hashed.
Firmware, HDDs and private cache payloads are excluded from publication.

Archive: 1,775,872 bytes; SHA-256
`570672d6e0958ad56c016e10a35a404705d4f1c9ec7eddb385f9083097d8e33c`.

The old counters/conflicts campaigns remain unstarted. New immutable campaigns
cover fresh production-parent A/A, parent versus repaired OFF, corrected dormant,
counters and conflicts. The new A/A has started; the rest remain staged. Runtime
mode is frozen for both builds, so active comparisons measure combined hooks and
accounting, not same-executable OFF/ON isolation. No production cache gain is
claimed; PR #299 remains draft and issue #167 open. Nothing was merged.
