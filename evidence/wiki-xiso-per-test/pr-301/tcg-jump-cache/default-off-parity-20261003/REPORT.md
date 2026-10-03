# Default-off parity against production parent

Decision: HOLD. Eight attempts completed correctly with complete evidence and qualified private Mesa namespaces. The runner's overall statistical verdict is **inconclusive**, because rewrite orders disagree. This is not a useful optimization gain and is not proof of universal default-off parity. The actual enabled candidate is a separate comparison.

| Test | Backend | Parent | Candidate OFF | Time saved | Improvement % | ABBA | BAAB | Correctness |
|---|---|---|---|---|---|---|---|---|
| code_stable | Vulkan | 2.514022 s | 2.522220 s | -0.008198 s | -0.33% | -0.91% | -0.23% | 8/8 pass |
| code_rewrite | Vulkan | 3.056267 s | 3.034374 s | +0.021894 s | +0.72% | +3.25% | -1.87% | 8/8 pass |

Statistic: median of four attempt mean guest work times; ten fixed-work iterations per attempt. `code_stable`: 50 million operations; `code_rewrite`: one million code-changing operations. Positive Improvement % is better: 100*(parent-OFF)/parent. Time saved = parent-OFF. The full report retains every min/mean/max measurement and all eight physical samples. A/A already showed +0.38% apparent stable change and -4.35% apparent rewrite change with identical executables; this is variation, not a correction to subtract. Stable OFF difference is -0.33%; rewrite's +0.72% aggregate switches from +3.25% ABBA to -1.87% BAAB and remains inconclusive.

Parent `76c23c7d`, executable SHA `5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b`; candidate product `5679cce0`, compile-disabled SHA `ce0004ed860673792254389a2b3533c868f8b474199796894ca55f49a885e7fd`. Matched GCC14/O2 i386-softmmu. OFF contains no victim metadata/helpers; primary layout and selected dispatch shape match, except source-line arguments and string relocation.

Deck .123, Vulkan/RADV, runner 55a360da, original XISO/suite/catalog/reference identities, explicit suite configuration and private cold Mesa namespaces. Full plan resolves each configuration hash and build/application identity. Physical A1 B1 B2 A2 B3 A3 A4 B4. All eight runs are correct and comparison-eligible, no cache waiver, changed expected result, manual input or retry. Suite-wide qualification remains unverified and full VM/retail coverage is incomplete.

The collector initially stopped because it treated the report's `inconclusive` statistical verdict as a data-admission failure. All eight records were indexed and eligible with zero blockers. That stopped log is preserved. The collector now admits complete indexed data with either eligible or inconclusive statistical verdict, while still requiring every correctness/evidence/cache check. No parity workload reran. This preserves uncertain and unfavorable measurements rather than treating disagreement as missing data.

EVIDENCE.zip contains all public attempt measurements, logs, reports, frozen plans, raw guest outcomes and the original stop transcript. INDEX.json inventories all 2,480 collected artifacts with local SHA-256/size; private state/cache bytes and redundant diagnostic ZIPs are manifest-only. Zero collection exclusions. Current [owning draft #301](https://github.com/Mainkill1/xemu/pull/301) remains HOLD.
