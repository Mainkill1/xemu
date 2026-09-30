# Issue 245 / PR 272: generated-code workload counters

**Status: diagnostic observations; no optimization or qualified Improvement.**

Related to [xemu issue 245](https://github.com/Mainkill1/xemu/issues/245) and
[draft product PR 272](https://github.com/Mainkill1/xemu/pull/272).
This is the existing unconditional-clear implementation with optional counters.
No lazy cache-retention candidate exists.

## Quick baseline and candidate comparison

| Fixed-work leaf | Previous main, quiet reference mean | Diagnostic candidate mean | Improvement | Decision |
| --- | ---: | ---: | --- | --- |
| Stable code, 50 million operations/sample | 2,761,269.7 us | 4,302,932.5 us | **Not qualified** | Probe is slower in these observations; no optimization benefit established |
| Code rewrite, 1 million operations/sample | 2,837,118.2 us | 5,764,821.5 us | **Not qualified** | Requires matched production builds and repeat testing |
| Fixed cycle baseline `bd1fecb` vs optimization | Not run under this procedure | No behavioral candidate | **Not measured** | Outstanding |

Previous-main observations come from the [CPU fixture qualification report](../research245-cpu-fixtures-20260930/REPORT.md), quiet run
`20260930-145635788-44b43fc0c8154f4f88e3d1bf80f6bba4`.
Previous main is `2d289cb349bca95eae81b6a54b0f8d965365ff82`, extracted CI executable
`5b3764acb93ee6d319b9cf7822295ae8748896b87b39ba52702838f1294f22f9`.
The diagnostic is a different native development build and includes three planned
HMP queries. Driver-cache state is unqualified. These are descriptive observations,
not a causal overhead estimate, regression percentage, or accepted speedup comparison.
Do not compare leaf timings against each other: their operation counts differ.

## Connection to existing behavior

Existing xemu invalidates an individual PC-relative TB by clearing all 4,096
jump-cache slots. This draft adds optional counters around those same clears and
dispatch lookups. Stable code exercises repeated execution with unchanged operands;
code rewrite alternates an aligned instruction operand with CPUID serialization.
The fixture's independent execution/input checksums test that the generated function
returns the required values. They do not cover remapping, page-spanning TBs,
concurrency, reset/load, or TB reclamation.

The stable control is **lower-invalidation, not zero-invalidation**. The windows
below show approximately 303–305 clears/s with unchanged code versus roughly
87,001–170,298/s for code rewrite. This supports using the pair for further
attribution. It does not establish that clearing causes the later global misses
or how much production execution time retention would save.

## Exact inputs and procedure

- Measured product code: `06168a2f455b832bc6eb7936ebe725ec530b1b00`; tree
  `67c893a6de2adadc92557aad7b9e86da4dc14926`.
- Deck executable: `9e2dbe1b9ff3298de99f57c9c00accab7cd25dad6362e7d92d7d4d63aed0bd53`;
  same frozen executable as the [PGR2 probe](../research245-probe-20260930/REPORT.md).
- Fixture code: `c02a1a44e9a4ee9804ac14c75bc431af6a507f64` in the tool repository.
  [Tool PR 49](https://github.com/Mainkill1/xemu-perf-tests/pull/49) owns fixture implementation;
  this xemu PR owns emulator measurements.
- XISO: `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`;
  default independent oracle `012286767ceb727d81f4646463cc46dcb13c9e7f30a891eeda74fba63f78401c`.
- Corrected base test revision: `f37bdc4a237c1370bc231c4a85545558f887eb239e1010514d9a0273d14629f6`;
  suite revision `3fbcb62b7205eaf649a069d6eb00caf65f05c024f255d78155657c4faf5c015d`.
- Steam Deck runner `0.2.0+6089e8b841bce379015500853c0455551d7fd2cf`;
  one selected leaf, zero warmups, multiplier one, ten measured samples.
- Plan: wait 8 s → `jit-early` → wait 10 s → `jit-middle` → wait 8 s → `jit-late`.
  All queries are maintained HTTP plan actions running HMP `info jit` without pausing.
  Each run records exactly three diagnostic interventions and zero transfers,
  manual inputs, pauses, screenshots, previews, or host-state changes.

Query boundaries are wall-clock offsets, **not guest marker boundaries**. They can
include boot, fixture execution and surrounding guest activity. The early/middle
window must not be labeled a pure workload phase. HMP fields are independently
atomic and can change during formatting. Rates use query invocation times as an
approximation; non-null counts are pointer observations, not unique evicted TBs.
Sample timing includes diagnostic overhead. No Windows result is claimed: its
runner was executing another job, and was not interrupted.

## Retained outcomes

| Named run | Execution / correctness | Required evidence | Mean, us | p95, us | Raw result |
| --- | --- | --- | ---: | ---: | --- |
| Stable-code original procedure | Completed, exit 0; 1/1 passed | **incomplete** | 4,291,994.8 | 4,356,509 | [result](runs/20260930-151149660-90bb6726facc4b7ab1dc841f714ba0f1/result.json) |
| Stable-code corrected procedure | Completed, exit 0; 1/1 passed | **complete** | 4,302,932.5 | 4,344,760 | [result](runs/20260930-151640365-5fa535de9f3144aa82fbbeff2c133f46/result.json) |
| Code-rewrite corrected procedure | Completed, exit 0; 1/1 passed | **complete** | 5,764,821.5 | 5,890,422 | [result](runs/20260930-151740512-a75c05789bc44022a728a53904910590/result.json) |

All three comparisons remain **ineligible**; no driver-cache waiver was used.
The original procedure required `diagnostic-bundle.json` during workload validation,
but the runner produces that bundle later. Its evidence remains incomplete even
though all three monitor queries eventually completed. The corrected immutable
procedure requires plan completion and all three request-ledger IDs. The retained
audit additionally checks each query's completed result and actual monitor content.
The frozen rewrite-v1 plan was never executed; it is retained as an unused plan,
not a test outcome. Nothing was retroactively relabeled.

## Diagnostic counter windows

| Named run / window | Approximate seconds | PC-relative clears | Clears/s | Mean non-null observations/clear | Global hits | Global misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stable-code original procedure: early → middle | 10.1301 | 3,057 | 301.8 | 73.18 | 300,228 | 22,075 |
| Stable-code original procedure: middle → late | 8.0086 | 2,439 | 304.5 | 67.14 | 213,718 | 2,557 |
| Stable-code corrected procedure: early → middle | 10.1562 | 3,075 | 302.8 | 73.39 | 302,084 | 22,116 |
| Stable-code corrected procedure: middle → late | 8.0360 | 2,447 | 304.5 | 67.48 | 214,563 | 2,666 |
| Code-rewrite corrected procedure: early → middle | 10.0458 | 873,986 | 87,000.6 | 2.29 | 1,191,787 | 1,763,319 |
| Code-rewrite corrected procedure: middle → late | 8.1825 | 1,393,467 | 170,298.3 | 2.16 | 1,644,975 | 2,784,050 |

Full TB flush count is zero in all nine snapshots. PC-relative clear deltas match
individual TB invalidation deltas; clear histograms and 4,096 traversed slots per
clear are consistent. Full TB flushes and jump-cache clears are distinct counters.
The probe does not connect each global miss to a preceding clear.

## Verification, evidence and remaining gates

`python3 audit.py` checks all three terminal contracts, input identities, exact
one-leaf coverage, normalized guest source hashes, nine completed monitor queries,
and counter/histogram invariants. It copies runner timing values; it does not
recompute benchmark metrics from raw CSV. `SHA256SUMS` pins the complete public
inventory, including frozen plans, raw receipts, logs and configurations.
Executables, firmware, EEPROM, guest disks and media binaries are excluded.

Product collector tests, sanitizers and CI are linked in the [PGR2 probe report](../research245-probe-20260930/REPORT.md).
The previous-main [full fixture campaign](../research245-cpu-fixtures-20260930/REPORT.md)
retains 15 correctness-failing attempts; this focused pass does not override them.
A matched baseline/candidate comparison, Morrowind, causal miss attribution and
production lifetime/remapping/concurrency/reset tests remain outstanding before
implementing or recommending cache retention. **Keep PR 272 draft.**
