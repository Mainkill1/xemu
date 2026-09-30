# Issue 245: matched CPU observer-mode checks

## Recommendation: HOLD

**Summary:** 48 leaf attempts (24 on each host) completed with correctness PASS and complete evidence. Enabled collection still costs time. These measurements apply to exact source `3a3d3c390fbb5d3835206d47614a86467e7a9330`, before the subsequent NV2A timing changes on main.
**Remaining:** Qualified cache conditions, matched parent versus compiled-OFF builds, the updated main timing context, broader Vulkan/OpenGL/control/game coverage, and a real safety-qualified retention candidate.

| Test ID | Host / backend | Before: same executable OFF | After: all mode | Time difference | Improvement % | Correctness |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `cpu_translation_blocks.code_stable` | Deck / Vulkan | 3.411576 s | 3.629059 s | +0.217483 s | -6.37% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | Deck / Vulkan | 3.035197 s | 3.153729 s | +0.118532 s | -3.91% | PASS / unqualified |
| `cpu_translation_blocks.code_stable` | Windows / Vulkan | 1.188613 s | 1.452770 s | +0.264157 s | -22.22% | PASS / unqualified |
| `cpu_translation_blocks.code_rewrite` | Windows / Vulkan | 2.213222 s | 2.256276 s | +0.043054 s | -1.95% | PASS / unqualified |

Improvement = 100 × (OFF − enabled) / OFF; positive means lower time. The fixed work is 50 million operations/sample for `code_stable` and one million for `code_rewrite`. Each mode has two independent process attempts, each with ten guest samples. The table uses the median of the two per-attempt guest medians. It is not suite wall time, host CPU time, FPS or a cache-retention result.

## Full results and controls

- [Deck: all eight OFF-versus-mode comparisons, every attempt and A/A controls](deck/REPORT.md).
- [Windows: all eight comparisons, every attempt and A/A controls](windows/REPORT.md).
- [Collector source/modes, matched host microbenchmark and sanitizer/negative checks](../research245-light-probe-20260930/REPORT.md).

Modes mean: `off` disables the collector; `counters` counts only; `occupancy` adds nominal 1/32 clear occupancy scans; `timing` adds nominal 1/1,024 lookup and 1/32 clear timings; `all` combines occupancy and timing. All cache-clearing writes remain. No retention implementation exists.

Every attempt is **comparison-ineligible** because driver-cache qualification is absent; no waiver is enabled. A private application cache and declared driver variables are not proof of driver isolation. Frequency/thermal state and other activity were not locked. Windows rewrite mode differences are nonmonotonic and run levels change over the sweep; do not rank modes or generalize overhead from two attempts. Historical unmatched CI-versus-development builds do not supply the missing matched parent control.

The sweeps used maintained HTTP clients on both independent hosts. Canonical bulk evidence was downloaded after each runner became idle. There were no HMP queries or operator intervention during measurement. Immutable suites/plans, per-run input/source receipts, ten raw guest samples, normalized results, comparison rejections, metrics and logs are retained. Binary/runtime driver-cache payloads are excluded from public storage; each exclusion retains its digest in the canonical inventory. Firmware, EEPROM, game media and emulator binaries are not published.

## Source applicability / recheck

Exact source/executable/runner/ISO/catalog identities and exclusions are in each host receipt. Product code changed upstream after these runs: `a05db7b2375d9c25fe61b6c914e83bab57fc49ca` incorporates NV2A virtual-time changes from PR271. These 48 runs are historical checks of `3a3d3c39`; they are not automatic whole-emulator proof for a rebased head. The collector source can separately remain byte-equivalent for the host-only benchmark.

Run `python3 audit.py` here to verify the full SHA-256 inventory, raw sample medians, all sixteen comparisons, A/A arithmetic, source/build/input pins and all comparison rejections. Both platform tables retain negative results. [The fixture implementation](https://github.com/Mainkill1/xemu-perf-tests/pull/49) belongs to the tool PR; these emulator results belong to xemu.

The branch was rebased onto main `a05db7b2375d9c25fe61b6c914e83bab57fc49ca`
as `e5afa40f6deb715c60beecba68ab4dcf463c85bb`. Collector/dispatch/helper/unit sources and QEMU atomic/timer
dependencies are byte-identical to the measured revision; the full emulator
includes the upstream NV2A timing changes. Current whole-emulator mode comparisons
remain a separate missing gate.
