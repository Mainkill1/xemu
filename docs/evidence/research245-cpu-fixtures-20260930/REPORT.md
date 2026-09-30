# Issue 245: CPU fixture qualification on previous main

**Status: Fixture checks pass; full suite fails correctness; no optimization Improvement measured.**

## Baseline and candidate comparison

| Role | Identity / meaning | Result |
| --- | --- | --- |
| Fixed cycle baseline | `bd1fecb93353272dda2a810991e28945de35b665` (retained cycle baseline; not tested in this stage) | No matched measurements; Improvement **not measured** |
| Previous main | `2d289cb349bca95eae81b6a54b0f8d965365ff82` | Existing product behavior; [baseline selection](../../performance/baseline-selection.json) distinguishes it from the fixed cycle baseline |
| Diagnostic candidate | `06168a2f455b832bc6eb7936ebe725ec530b1b00`, draft [PR 272](https://github.com/Mainkill1/xemu/pull/272) | Counts existing clearing/lookup work; keeps unconditional invalidation |
| Behavioral optimization candidate | **None** | No lazy retention implementation or production Improvement result |

Positive Improvement would mean better useful guest progress or lower CPU/frame
intervals. Missing matched measurements stay explicitly unmeasured. Collector
ON/OFF observations measure instrumentation; they are not baseline-versus-optimized
product results. No faster xemu candidate has been established by this stage.



## Result

[Draft fixture PR 49](https://github.com/Mainkill1/xemu-perf-tests/pull/49) adds unchanged-code and code-rewrite workloads needed by [xemu research 245](https://github.com/Mainkill1/xemu/issues/245). On exact current-main xemu, the final focused campaign and its quiet repeat passed both leaves; the containing `CpuTranslationBlocks` campaign passed all five leaves. Both had complete evidence, exact selection receipts, and exit code zero. The new leaves matched independently computed input signatures and execution checksums.

This establishes usable Xbox CPU fixtures. It does not establish a jump-cache optimization, the stable leaf's actual invalidation rate, or a qualified performance comparison. The [draft attribution probe](https://github.com/Mainkill1/xemu/pull/272) still needs workload-specific counter capture and the issue's lifetime/correctness gates before any retention change.

The full 156-leaf, 17-attempt campaign finished with two attempts passing runner correctness and fifteen failing. All attempts completed with exit zero and complete evidence, with no operator interventions recorded. All 156 leaf records and five structural group records reported guest PASS, but this does not override the runner failures: 18 leaves lacked a pinned reference; the runner reported 68 framebuffer mismatches against historical references, with unchanged fixed-work fields. Direct byte comparisons find two additional historical framebuffer differences in chunks whose missing-reference coverage prevented per-leaf evaluation. The CPU chunk passed all seven leaves, including both new fixtures. The causes of the graphics mismatches remain unclassified. See the frozen full plan, terminal status/attempt list, per-run sources, `full-correctness-review.json`, and `summary.json`. Every original failure is retained.

## Identity and controls

- Fixture source: `c02a1a44e9a4ee9804ac14c75bc431af6a507f64`; tree `a4c4ea4d2c7d39b177721a94393de95bc1ed70c3`.
- Exact main xemu source: `2d289cb349bca95eae81b6a54b0f8d965365ff82`; executable SHA-256 `5b3764acb93ee6d319b9cf7822295ae8748896b87b39ba52702838f1294f22f9`.
- Release build: Clang 19.1.7, pinned NXDK `73c95900965a16be3a3e34b8d4d5d41bc18498be`. Other dependency commits are in `build-identity-final.json`.
- Final XISO SHA-256: `74a10c400f4fb280dcb4037e38e7e4d3150e73e0061a04f65b3c68cc61a49e63`.
- Catalog: `sha256:34b8ee7f91f3412b5e7758aab9f4ca08746ef2e97fee8b5c61be8947e145f7f4`; 156 leaves, five structural groups. The runner read the catalog from the actual ISO during registration.
- Independent default-work oracle SHA-256: `012286767ceb727d81f4646463cc46dcb13c9e7f30a891eeda74fba63f78401c`.
- Steam Deck, runner `0.2.0+6089e8b841bce379015500853c0455551d7fd2cf`, Vulkan, 128 MiB guest RAM, private HDD/EEPROM and application state. Exact machine/configuration receipts are retained with each run.
- All launches, selections, waits, and extraction used the maintained HTTP clients from the build host. No SSH guest launch, queue edit, or cache waiver was used.

## Work and independent oracles

Both leaves execute a generated `mov eax, imm32; ret` from a separate executable allocation and CPUID leaf zero before every call. The stable leaf leaves its immediate unchanged. The rewrite leaf alternates two constants with an aligned volatile store. The uint32 recurrence rotates left five, XORs the returned value, and adds the operation index multiplied by `0x9e3779b9`.

Every warmup and measured invocation contributes to a sticky mismatch flag. Allocation, initialization, deallocation, assertions, input fingerprinting, metadata serialization, and drawing are outside `Profile`. Its body includes the work loop, result stores, and one comparison per invocation. See [the exact workload documentation](https://github.com/Mainkill1/xemu-perf-tests/blob/c02a1a44e9a4ee9804ac14c75bc431af6a507f64/docs/cpu-code-rewrite.md).

| Revision 2 leaf | Operations per invocation | Input signature | Execution known answer |
| --- | ---: | --- | --- |
| CodeStable | 50,000,000 | `9275e7c3` | `f5ff3985` |
| CodeRewrite | 1,000,000 | `02a5f4ff` | `65151d67` |

Known answers came from independent integer recurrence and FNV-1a calculations, then real native execution. The static oracle contains fixed work fields and mathematical hashes; it contains no emulator timings or observed framebuffer golden. For the containing suite, its two records were authored with three warmups and multiplier four. The 141 historical reference records were retained unchanged. Their exact bytes are in `historical-reference-results.txt`; the actual 143-record oracle is `cpu-code-rewrite-suite-reference.json`. `audit.py` verifies the recorded hashes, unchanged historical records, two explicitly adjusted additions, and the saved template’s pinned suite-oracle hash. The execution recurrence resets on each invocation, so its known answer remains the same under that explicit repetition contract. Missing historical coverage in the full suite remains a failure, not an implicit acceptance.

## Verification

- 137 maintained host contracts pass. The new native fixture executes thirteen literal recurrence vectors, including the 50-million stable batch. Suppressing actual code stores in a temporary build must fail the two-operation rewrite checksum (`dddf8872` expected, `5e8f6aff` stale).
- A review found that the host oracle validator requires a work/result checksum pair. The new regression reproduced its failure before the fix. The fixture now emits an FNV-1a fingerprint of fixed inputs and a separate actual execution checksum; the validator retains both. Three focused native/integration contracts pass after the fix.
- Release Xbox build, catalog generation check, and diff check pass. Changed CPU sources introduce no warnings; the final link retained the existing `.edata` merge warning.
- Read-only review found no remaining blocking defects. Native tests establish the helper's behavior; the runner results below establish Xbox execution on exact main.

## Retained outcomes

| Campaign/run | Execution | Correctness | Evidence | Comparison |
| --- | --- | --- | --- | --- |
| Initial seed, `20260930-134802833-c17f80c9fba04f9abd6371c106620207` | Invalid input; no xemu process | Not evaluated | Incomplete | Ineligible |
| Revision 1 calibration, `20260930-135154608-9b1b894fe8334781a059affd86a33911` | Completed, exit 0 | Failed: no pinned oracle for two new leaves | Complete | Ineligible |
| Final focused pair, `20260930-141819901-885b07d775754126a98447c7cafa5acb` | Completed, exit 0 | Passed, 2/2 leaves | Complete | Ineligible |
| Containing suite, `20260930-141936846-980988c02f894b4f9ca6cea40ecd135c` | Completed, exit 0 | Passed, 5/5 leaves | Complete | Ineligible |
| Quiet focused repeat, `20260930-145635788-44b43fc0c8154f4f88e3d1bf80f6bba4` | Completed, exit 0 | Passed, 2/2 leaves | Complete | Ineligible |

The first seed had a QCOW2 v3 header length of 112, while the injector supports length 104. A separate QCOW2 v2/compat-0.10 container was prepared from the original. `qemu-img compare` confirmed identical guest-visible bytes, `qemu-img check` found no errors, and the runner verified private configuration injection/readback. Both source and conversion hashes are retained. The original asset and failed campaign were preserved.

Initial revision 1 calibration reported about 58 ms per one-million stable invocation and 2.85 s per rewrite invocation. The stable workload was increased using this baseline, then both leaves moved to revision 2 to preserve the earlier contract. The intermediate revision 2 image with incomplete checksum metadata was uploaded but never executed; only the final superseding image is qualification evidence.

## Performance checks

Values below are the runner's stored `mean_us` and `p95_us` measurements, taken directly from canonical results. Original guest samples and normalized source hashes are retained. No metrics were reconstructed from CSV.

| Campaign / work settings | Leaf | Mean (us) | p95 (us) |
| --- | --- | ---: | ---: |
| Focused; warmups 0, multiplier 1 | CodeStable | 3,148,610.8 | 4,477,057 |
| Focused; warmups 0, multiplier 1 | CodeRewrite | 2,869,808.6 | 2,921,235 |
| Containing suite; warmups 3, multiplier 4 | CodeStable | 3,023,154 | 4,484,743 |
| Containing suite; warmups 3, multiplier 4 | CodeRewrite | 2,860,690.6 | 2,917,437 |
| Quiet repeat; warmups 0, multiplier 1 | CodeStable | 2,761,269.7 | 2,826,036 |
| Quiet repeat; warmups 0, multiplier 1 | CodeRewrite | 2,837,118.2 | 2,871,816 |
| Containing suite; warmups 3, multiplier 4 | DirectLoop | 19,401.6 | 19,914 |
| Containing suite; warmups 3, multiplier 4 | IndirectDispatch | 62,257.4 | 64,490 |
| Containing suite; warmups 3, multiplier 4 | IndirectDispatchStress | 1,235,186.1 | 1,250,415 |

Both new batches now take seconds. The stable leaf's long tail remains visible. Different operation counts prevent direct timing comparison between leaves. These are calibration/fixture measurements on one emulator build, not a before/after speedup.

The runner marked all completed comparisons ineligible. Driver-cache control remained unverified. It recorded two preparation transfers during the earlier focused run and one during the containing suite. Those interventions and the unfavorable stable tail are retained. The distinct quiet repeat passed with zero bulk transfers and no operator interventions; it remained comparison-ineligible because driver-cache control was unverified. No further preparation occurred during that repeat. No permissive cache policy was enabled.

## Limits and next gates

The fixture covers one allocated page and one aligned immediate. It does not cover remapping, spanning instructions/pages, multiple CPUs, reclamation/address reuse, debugger identity, or reset/load transitions. It also does not measure the stable leaf's invalidation rate or causal post-clear misses. Windows execution and graphics mismatch investigation remain pending.

Canonical preflight result metadata contains diagnostic entries whose timestamps/directories belong to an earlier PGR2 run. They are retained verbatim and excluded from this report's CPU analysis. All timing and checksum rows above come from the matching run's guest output and runner normalization, with source hashes verified in `summary.json`.

This evidence supports draft fixture publication and continued attribution work. It does not support changing the unconditional PC-relative cache clears. PRs remain drafts; no merge or ready transition is authorized.

## Evidence location

Emulator measurements now belong in **Mainkill1/xemu**, alongside draft PR 272. Fixture implementation belongs to the benchmark tool’s [PR 49](https://github.com/Mainkill1/xemu-perf-tests/pull/49); its code location is separate from these emulator results. `ORIGINAL-REPORT.md` and `source-artifact-hashes.json` preserve the prior report and manifest. `verify_migration.py` verifies the original records and the migrated manifest.
