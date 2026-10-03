# Corrected jump-cache observer cost on Steam Deck

**Decision: HOLD.** With the parent code-shape regression repaired, compiling in
the dormant observer has measurable cost in the stable-code fixture. Combined
medians are below 5%, but one order is not; these results do not establish an
under-5% overhead bound. Active counters/classifier qualification remains pending.
No production cache optimization or cache speedup is tested here.

## Reference and candidate

Both builds use product `31c083ece2e7d44e87b2c7c4039cfc6b2365848d`, matched
GCC14 O2 i386-softmmu settings and support bytes. A compiles the probe out; B
compiles it in with collection **OFF**. Both inline the primary lookup. This
isolates the build-option change more closely than the
[superseded compiler-confounded comparison](../observer-dormant-7cb-20261003/REPORT.md).
It is not a same-executable runtime OFF/ON comparison.

| Label | Build | Executable SHA-256 |
| --- | --- | --- |
| A | Corrected probe compiled out | `6ab70e58239e9b312f4168b3d8572e64143fcb602adf7264bd3a1e267e7cea7c` |
| B | Corrected probe compiled in, collection OFF | `db2edddae3080e26f166b4e9750d28449e1150604b20f9135aa884cebb3a5d58` |

Physical order is A1, B1, B2, A2 (ABBA), then B3, A3, A4, B4 (BAAB).
All attempts and samples remain. Tables use medians of four **attempt-level mean**
guest work times; each order uses two attempts per label. Positive Improvement %
means less time: `100 * (A - B) / A`. Vulkan/RADV throughout.

## Baseline versus dormant candidate

| Test ID / fixed work | Baseline A | Dormant B | Time saved | Improvement % | ABBA | BAAB | Correctness |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `cpu_translation_blocks.code_stable`, 50 million operations | 2.512131 s | 2.557325 s | -0.045195 s | -1.80% | -9.18% | -2.10% | PASS 8/8 |
| `cpu_translation_blocks.code_rewrite`, 1 million operations | 3.064022 s | 3.096189 s | -0.032167 s | -1.05% | -3.66% | -1.55% | PASS 8/8 |

Stable-code B1 has mean 2.944436 s versus 2.554232–2.557663 s in the other B
attempts. Its first three stable samples were 3.433507, 4.073663 and 3.793782 s;
the remaining seven were 2.551433–2.635957 s. Its rewrite mean was 3.294638 s.
The cause is **unclassified**. It is not discarded, repeated or dismissed as
host noise. The -9.18% ABBA mean-work result matters even though the combined
median is -1.80%.

The compact maintained report uses attempt-level medians instead: stable -2.20%
(ABBA -3.17%, BAAB -2.16%); rewrite -0.87% (ABBA -2.98%, BAAB -1.63%). Full
reports preserve mean, median, minimum, maximum, p95 and raw samples. Ten samples
per leaf make nearest-rank p95 equal to the maximum; no resolved tail or confidence
interval is claimed.

The [fresh parent A/A and default-off parity](../corrected-parent-parity-20261003/REPORT.md)
give context: identical-parent A/A mean-work differences -0.11% stable / +3.35%
rewrite; default-off parent parity -0.03% / -0.20%. Small rewrite effects are
inconclusive. Do not pool campaigns or infer a universal noise floor. These are
guest work timings, not whole-game FPS or suite duration.

## Qualification and identities

- Campaign `i167-correct-dormant-31c-001`; all 8/8 attempts completed, passed
  original references and were evidence-complete/comparison-eligible.
- Steam Deck `10.0.0.123`, maintained LAN HTTP only. Runner
  `c264004dfc906eef008c8a7235764c37daee330b` includes main #93/#94/#95 fixes.
- ISO `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`;
  catalog `sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4`;
  pinned `c02a1a44` CPU fixtures, 128 MiB, zero warmups, multiplier one,
  per-iteration completion. No monitor queries, external sampler or guest clock changes.
- Suite remains **unverified**, not selfapproved. Exact suite/application hashes,
  frozen plans, run IDs and outcomes are in the archive.
- All 8 private cold Mesa disk namespaces start empty and qualify with observed
  writes, zero issues and no uncontrolled-cache waiver or global purge. OS page
  cache and driver in-memory state remain uncontrolled.

All **2,482 native files** were collected with **zero exclusions**. The archive
publishes 280 native evidence files; [INDEX.json](INDEX.json) individually hashes
the 2,202 private runtime/cache files retained locally. Guest extraction bytes
match their ledger. No native failure, silent rerun or reference substitution
occurred. [SUMMARY.json](SUMMARY.json) and [EVIDENCE.zip](EVIDENCE.zip) retain every
metric and attempt, including B1. Archive size/SHA-256 are in INDEX.json.

## Remaining work

The corrected counters ABBA/BAAB campaign is running. Conflicts remains staged
and unstarted. Those compare compiled-out A against compiled-in active B, so
they measure combined hook and accounting cost rather than same-binary activation.
After qualification, obtain a reached retail scene with explicit controller
binding and a 300-second diagnostic window. Counts or slow instrumentation alone
cannot establish whether a production victim cache helps. Keep PR #299 draft.
