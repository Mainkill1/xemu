# Corrected parent A/A and default-off parity on Steam Deck

**Decision: HOLD.** The repaired default-off build performs close to its matched
production parent in these two CPU fixtures. The earlier apparent 23.82% stable
gain was a compiler confound, not a cache optimization. No production victim
cache or measured cache speedup is present. Corrected observer-cost qualification
and reached-retail attribution remain required.

## What A and B mean

| Campaign | A: reference | B: candidate | Question |
| --- | --- | --- | --- |
| Parent A/A | Production parent `76c23c7d444` | Identical parent executable bytes | How much variation occurs without a code change? |
| Default-off parity | Production parent `76c23c7d444` | Repaired `31c083ece2e`, probe compiled out | Does the default-off branch retain parent performance? |

Each campaign physically executes **A1, B1, B2, A2** (ABBA), then
**B3, A3, A4, B4** (BAAB). Labels identify repetitions, not workload variants.
There are four attempts per label and ten fixed-work guest samples per leaf in
each attempt. Tables use the runner's median of four **attempt-level means**;
ABBA and BAAB each use two attempts per label. Positive Improvement % means less
time: `100 * (A - B) / A`. These are guest work times, not FPS or suite wall time.

## Baseline, candidate and order results

Backend is Vulkan/RADV throughout. Correctness passes both original references
in all eight attempts per campaign; no expected results were replaced.

| Campaign | Test ID / fixed work | A time | B time | Time saved | Improvement % | ABBA | BAAB | Correctness |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Identical-binary A/A | `cpu_translation_blocks.code_stable`, 50 million operations | 2.539755 s | 2.542468 s | -0.002712 s | -0.11% | +0.02% | -0.68% | PASS 8/8 |
| Identical-binary A/A | `cpu_translation_blocks.code_rewrite`, 1 million operations | 3.125645 s | 3.021081 s | +0.104565 s | +3.35% | +3.01% | +1.00% | PASS 8/8 |
| Parent → repaired default-off | `cpu_translation_blocks.code_stable`, 50 million operations | 2.527315 s | 2.528067 s | -0.000752 s | -0.03% | -0.05% | -0.04% | PASS 8/8 |
| Parent → repaired default-off | `cpu_translation_blocks.code_rewrite`, 1 million operations | 3.008086 s | 3.014005 s | -0.005919 s | -0.20% | -2.43% | +0.10% | PASS 8/8 |

**The A/A differences are variation, not gains.** Small rewrite effects remain
inconclusive: identical bytes differed by +3.35%, and the parity rewrite orders
straddle zero. Neither a universal noise floor nor a confidence interval is
established by these eight-attempt controls. Default-off parity does not establish
enabled observer cost or whole-game performance.

The maintained report's compact default table uses attempt-level **medians**,
which produce different numbers: parity stable -0.27%, rewrite -0.49%; A/A stable
-0.20%, rewrite +3.07%. Full JSON/CSV retain mean, median, minimum, maximum and p95
metrics and every attempt. With ten guest samples, nearest-rank p95 equals the
maximum; it is not a well-resolved tail estimate. No unfavorable metric is omitted
from the archive.

## Exact identities and controls

- Host: Steam Deck `10.0.0.123`; maintained LAN HTTP for launch, waits and collection.
- Matched fresh GCC14 O2 i386-softmmu builds; 128 MiB, zero warmups, multiplier one,
  per-iteration completion, no guest clock changes, HMP queries or external sampler.
- Parent executable SHA-256:
  `5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b`.
- Repaired default-off executable SHA-256:
  `6ab70e58239e9b312f4168b3d8572e64143fcb602adf7264bd3a1e267e7cea7c`.
- Support files/configuration/seed bytes match. All build and application identities
  are in the contract. Source repair restores the parent's literal lookup predicate;
  disabled source equivalence and both builds were checked in the
  [compiler/correction packet](../observer-dormant-7cb-20261003/REPORT.md).
- Runner `c264004dfc906eef008c8a7235764c37daee330b` includes current main
  `23763249` and controller #93, scene admission #94, portable identity #95 fixes.
- ISO `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`;
  catalog `sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4`;
  unchanged pinned `c02a1a44` CPU fixtures. Portable plans match.
- Campaigns: `i167-correct-aa-31c-001` and `i167-correct-parity-31c-001`.
  Frozen plans, all run IDs, suite revisions and complete outcomes are archived.
  Both suites remain **unverified**; comparison eligibility is not baseline approval.

## Mesa qualification and evidence

All **16/16** attempts completed, passed correctness and were evidence-complete
and comparison-eligible. Every private cold Mesa disk namespace started empty
and qualified with observed writes, zero issues and no uncontrolled-cache waiver.
A/A ended with 275 files / 2,886,729 bytes in each run. Parity ended with 275 or
276 files / 2,886,729 or 2,886,974 bytes; the full receipts preserve that variation.
No global cache purge was used. OS page cache and in-memory driver state remain
disclosed uncontrolled inputs.

All **4,963 native files** were collected with **zero exclusions**: A/A 2,480,
parity 2,483. [INDEX.json](INDEX.json) hashes every file. The archive publishes
560 native evidence files and retains individual hashes for 4,403 private
runtime/cache files kept locally; it excludes private payloads and emulator
binaries. Collection verifies extracted guest-result bytes against their ledger.
No native attempt failed, was silently rerun or had a reference selfapproved.
The preserved portable-check CLI argument error occurred before execution.

[SUMMARY.json](SUMMARY.json) contains all metric rows and cache receipts.
[EVIDENCE.zip](EVIDENCE.zip) contains full native results, assessments, frozen
settings, manifests, per-attempt statistics and maintained comparison reports.
Its size and SHA-256 are in INDEX.json.

## Next bounded check

Measure corrected compiled-in dormant, counters and conflicts modes against the
same repaired compile-disabled executable, preserving ABBA/BAAB and Mesa gates.
These comparisons measure combined compiler hooks and accounting, not
same-executable OFF/ON isolation. Then obtain reached-retail attribution before
deciding whether an uninstrumented miss-only victim experiment is worthwhile.
No optimization result is inferred from diagnostic overhead; keep PR #299 draft.
