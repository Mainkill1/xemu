# Issue #167: jump-cache attribution on Steam Deck

**Decision: HOLD.** The first deliverable is implemented and natively exercised:
miss classification and FIFO victim recoverability. No production cache candidate
or speedup has been tested. These counts justify a bounded victim-cache experiment;
they do not justify readiness or a predicted FPS improvement.

**Latest qualification:** [corrected parent A/A and default-off parity](corrected-parent-parity-20261003/REPORT.md)
complete 16 correct, eligible runs with qualified private Mesa namespaces. Mean-work
parity is -0.03% stable / -0.20% rewrite; identical-parent A/A variation is -0.11% /
+3.35%. Product correction is `31c083ece2e`; the original attribution packet below
describes `7cb38844fb2`. The [corrected dormant observer packet](corrected-observer-cost-20261003/REPORT.md)
adds eight correct/eligible/Mesa-qualified runs: -1.80% stable / -1.05% rewrite mean
work, with stable ABBA -9.18%. It does not establish an under-5% cost bound.
The same packet now adds eight counters outcomes: -4.65% stable / -3.64% rewrite;
stable BAAB and rewrite ABBA exceed 5%. Conflicts measurement is running.

## What changed and why

Product `7cb38844fb2dddb1b24cc65501831c9cdcedb43e`, based on
`76c23c7d444a6f12c9778bb2c35fab513f6c8056`, retains the lighter collector from
closed #272 and adds explicit `conflicts` mode. Original full-key lookup, real
translation/global hash paths and atomic cache clears remain. The diagnostic
models cannot execute, dereference or populate the real cache with shadow tokens.

An occupied slot does not prove a recoverable miss. The diagnostic compares its
displaced virtual PC and opaque TB token with the TB returned by the **real,
validated global lookup**. Eight- and sixteen-entry FIFO models evolve separately.
All invalidations discard their history. Per-primary-slot taint excludes entries
published across invalidation, including generation where its epoch is captured
before code generation releases page ownership. Models occupy 4,704 bytes in this
x86-64 build (GDB `sizeof`), including 4,096 taint flags. This is diagnostic storage,
not the issue's proposed cache allocation budget.

See [field definitions and limits](../../../../docs/performance/tcg-jump-cache-probe.md).

## Readable attribution results

The following are **late minus early HMP snapshots**, approximately 18 seconds
apart, with the selected leaf executing in a private XISO process. They are
wall-clock windows, not exact guest phase boundaries; boot/setup and surrounding
activity may be included. Statistics are independently atomic and may lag owner
publication. Percentages below divide potential recoveries by validated global
hits; they are **not Improvement %** and cannot be read as time or FPS saved.

| Selected fixed-work leaf | Validated global hits | Eight-entry potential recoveries | Sixteen-entry potential recoveries | Shadow resets | Correctness |
| --- | ---: | ---: | ---: | ---: | --- |
| `cpu_translation_blocks.code_stable` | 514,708 | 81,718 (15.88%) | 90,412 (17.57%) | 5,475 | PASS |
| `cpu_translation_blocks.code_rewrite` | 4,817,029 | 44,452 (0.92%) | 53,206 (1.10%) | 4,261,674 | PASS |

Stable-code eight-entry recoveries represent only 0.01271% of completed C lookups
in that window; rewrite recoveries represent 0.03015%. Generated fast lookup and
direct chaining bypass these counters. The rewrite workload has millions of
invalidation generations and mostly empty-slot/new-translation misses; collision
counts alone would misidentify the dominant mechanism. Most cumulative recoveries
were already present at the early query, so cumulative boot totals are not used
as the workload result.

Conservative whole-shadow reset can discard unrelated surviving entries. Low
recoverability does not reject every precise-invalidation or two-way design.
No two-way, larger direct map, or actual victim-cache execution is implemented.

### Performance status

| Test ID | Backend | Baseline | Diagnostic mean work time | Time saving | Improvement % | Correctness |
| --- | --- | --- | ---: | --- | --- | --- |
| `cpu_translation_blocks.code_stable` | Vulkan/RADV | Not measured | 2.654602 s per 50 million operations | Not established | Not established | PASS |
| `cpu_translation_blocks.code_rewrite` | Vulkan/RADV | Not measured | 3.180570 s per million operations | Not established | Not established | PASS |

These are the runner's actual per-leaf means, ten guest samples in each single
instrumented run, not suite wall time. There is no matched baseline, A/A,
ABBA/BAAB or observer-cost comparison in this packet. The runner's retained
comparison reports correctly withhold numeric gains because these are unpaired
single-attempt diagnostic campaigns. Do not compare these times against older
unmatched CI/native builds.

## Native identities and state

- Host: Steam Deck `10.0.0.123`; launches, waits and collection via maintained LAN HTTP.
- Runner `c264004dfc906eef008c8a7235764c37daee330b`, incorporating main
  `23763249` / #95 portable saved-test identities plus retained draft #89 process support.
- Instrumented executable SHA-256:
  `3cfe630b1e210a95bf48aa3df3141ff5cc9ab024112242b4262fa4ff3202c272`.
- Fresh GCC 14 diagnostic O2 i386-softmmu build, not a matched release comparison.
  Unchanged support libraries/config/seed are hashed in the build receipt.
- Suite revision `961b09790da116a00bd273fd563ebf365277a0a7dc1a923707c3ab966c1ecc62`.
- ISO `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`;
  pinned `c02a1a44` CPU fixtures, 128 MiB, zero warmups, multiplier one,
  per-iteration completion. Selected leaves match two existing reference hashes each.
- Stable run `20261003-033132721-39f31032720541f5a410e1a4d3c17688`.
- Rewrite run `20261003-033330643-c088b3247fa24aaa9ff4935caef4d0b2`.

Both attempts completed, passed correctness, had complete evidence, and were
individually comparison-eligible. The suite remains **unverified**; attempt
eligibility is not baseline approval. Both private Mesa disk namespaces qualified
with observed writes, zero state issues, and no uncontrolled-cache waiver. OS page
cache and driver memory cache remain disclosed limitations. No global cache was
purged. All three monitor queries were successful in each run.

## Checks and preserved failures

| Check | Result |
| --- | --- |
| Current-head emulator probe OFF and ON | Build passed; OFF executable has no probe symbols |
| Maintained collector/validity/conflict checks | 9 + 7 + 13 passed locally, including default-off, and natively |
| Full GCC14 `check-unit` | Failed to build unchanged resampler test: missing `sinf`/`cosf` declarations; not full-suite green |
| Default-off conflicts fixture | Reproduced macro/include-order compile failure, then corrected and passed |
| Full/partial clear negative controls | Initially reported a false recovery; hooks corrected, controls pass |
| Late-publication model negative control | Initially recovery 1 when expected 0; taint correction passes, including generated publication |
| Source review | Default-off and generated-publication findings fixed; no remaining Important/Critical finding in bounded recheck |

The native standalone checks have exact `--tap` arguments, no QMP/automatic xemu
diagnostics, successful assertions and complete archival. Their run IDs and binary
hashes are in the packet. These are real maintained test files, not disposable
standalone arithmetic examples. Review and these model/helper controls do not
prove concurrent production call-site ordering on weak-memory hosts or whole-guest
remapping, storage reuse, reset/load and SMC correctness for a future cache.

One rewrite selection request was rejected before execution because its ID
exceeded 38 characters; the corrected ID was used once. Both receipts are retained.
There were no failed native attempts, silent reruns or selfapproved references.

The runner update passes 21/21 Linux/Windows CI checks and fresh 142 API, 48 runner,
23 XISO, 16 state and 6 finalization checks. Deployment verified 59 package files;
all 571 prior archived jobs, 238 saved identities and five checked native outcomes
survived. After these five new attempts, the Deck was idle with 576 archived jobs.

## Evidence inventory

[SUMMARY.json](SUMMARY.json) holds observations and explicit calculations;
[INDEX.json](INDEX.json) individually hashes public and private native files.
[EVIDENCE.zip](EVIDENCE.zip) contains raw per-leaf results, HMP output, native
assessments/configurations/state receipts, local build/negative-control logs and
runner qualification records. Full native collection retained **697 files with
zero exclusions**. Of those, 151 public files are in the archive; 546 private
runtime/cache payloads stay locally retained and individually indexed. No firmware,
HDD or private cache payload is uploaded. The archive also has 134 developer/runner
payloads and its embedded inventory.

Archive SHA-256: `b4a7bed8be9bdfcd49680544c4fb71b6fef977d65b025c52e60eacc07791ac35`.
Archive size: 561,899 bytes.

## Next experiment

The [updated-runner A/A packet](observer-aa-20261003/REPORT.md) adds eight
correct, evidence-complete, eligible runs with qualified private Mesa namespaces.
Identical executable bytes differ by -0.40% on stable mean time and -4.25% on
rewrite mean time; small rewrite changes cannot be accepted as gains.
The [completed dormant comparison](observer-dormant-7cb-20261003/REPORT.md)
adds eight further passing/eligible runs. Its apparent +23.82% stable gain is
confounded by an outlined compiled-out lookup versus an inlined enabled lookup;
it is not a cache optimization. Repair `31c083ece2e` restores the parent's
original disabled expression and passes both builds/all 29 checks. Fresh whole
parent and corrected binaries have new campaign identities. The
[corrected A/A and parent/default-off parity packet](corrected-parent-parity-20261003/REPORT.md)
contains all 16 completed outcomes and per-test reports. Corrected dormant
qualification has completed in the [observer-cost packet](corrected-observer-cost-20261003/REPORT.md).
Counters has completed and conflicts is running. Every B1 slow sample is
preserved, with cause unclassified.

First isolate OFF/counters/conflicts observer costs and obtain a reached retail
workload; any PGR2 measurement lasts at least 300 seconds. Then test one simple
miss-only eight-entry victim candidate against matched uninstrumented production
builds with A/A, physical ABBA and BAAB. Preserve mapping/page-spanning, SMC,
full-flush/reuse, CPU reset/load, debug/IRQ and concurrency controls. Explore the
sixteen-entry/equal-entry-budget two-way/larger direct alternatives only as bounded
separate variants with their memory and clear costs visible. A failed correctness
gate blocks readiness; no useful measured gain after adequate evaluation means
close with findings. No merge is authorized.
