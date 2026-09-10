# PR #70 evidence index

**Decision: HOLD.** Warm SPIR-V reuse works in the exercised cases. Automated PGR2 full-start tails exceed the acceptance limit; the manual race is exploratory and several qualification gates remain open.

**Tested source:** `b14bfb745870faae500a1ecb0ff49d2143ba8bd1`\
**Executable SHA-256:** `d6c0762fd672932667537b7152bf4068a7c313d2980d4b545e020e852064bc9d`\
**Evidence revision:** `303c381f6e7891101996f95f7e2c009b3abc559b`

| Evidence | Location |
| --- | --- |
| Readable report | [Branch report](../../../docs/performance/vulkan-spirv-prewarm.md) |
| Reports and results | [Automated and manual qualification](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr70-spirv-prewarm-20260910/full-qualification/REPORT.md) |
| Baseline, candidate, build, test identity | [Manifest](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr70-spirv-prewarm-20260910/full-qualification/manifest.json) |
| Per-workload measurements | [Automated result files](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr70-spirv-prewarm-20260910/full-qualification/automated) |
| Two-minute race procedure and measurements | [Manual race](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr70-spirv-prewarm-20260910/full-qualification/manual-race/RECIPE.md) |
| Cleanup and remaining lifecycle gates | [Receipt](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr70-spirv-prewarm-20260910/full-qualification/lifecycle-receipt.json) |
| Rejected harness attempts | [Preserved reasons](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr70-spirv-prewarm-20260910/full-qualification/rejected-harness-attempts.json) |
| Profiling | [PR #68 source-identity evidence](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr68-shader-identity-20260910/classification/REPORT.md) |
| Integrity | [SHA-256 list](https://github.com/Mainkill1/xemu-perf-tests/blob/303c381f6e7891101996f95f7e2c009b3abc559b/docs/evidence/pr70-spirv-prewarm-20260910/full-qualification/SHA256SUMS.txt) |
| Visual and raw evidence | Retained privately; copyrighted screenshots, game assets, raw traces, and writable disks are excluded from public source history. |

The public recipe records the new manual protocol. Machine-specific automation is preserved by hash and still needs parameterization before public reuse. Incomplete host/settings metadata and partial GPU sampling are explicit manifest gaps. The fixed baseline was not moved or rebuilt.
