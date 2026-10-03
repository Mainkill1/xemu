# #167 publication exchange: controlled comparisons remain inconclusive

## Decision

**HOLD for owning xemu PR #301; do not integrate this experiment.** The original eight-entry runtime remains unchanged. A two-line publication change preserves the required full barrier and passes the retained helper contracts, but clean Deck measurements do not demonstrate a dependable incremental performance gain. Stop this publication-only tuning pass; preserve the source, packages, maintained fixtures and complete evidence. The broader issue remains open because earlier fit-case benefit and above-capacity regression remain unresolved.

## What changed and why

Experimental source [1beff07a535acf153bf774ec578d5146b0143d23](https://github.com/Mainkill1/xemu/commit/1beff07a535acf153bf774ec578d5146b0143d23), based on owning branch head `587a832b3d0`, changes only `tcg_victim_cache_fill()` in `accel/tcg/tb-victim-cache.c` relative to original runtime `5679cce099eb777977e761245de47e77d05d0c05`:

```text
BEFORE: store primary TB pointer → full barrier → recheck epoch → clear if stale
AFTER:  atomic store with full barrier → recheck epoch → clear if stale
```

`qatomic_set()` plus `smp_mb()` becomes QEMU's documented `qatomic_set_mb()` primitive. On this x86 build, the separate pointer store and locked stack operation become one `xchg` pointer publication. No barrier is removed; the returned old pointer is discarded. Lookup keys, owner-local FIFO metadata, foreign invalidation protocol, active-writer/epoch checks, storage lifetime and default-off guards remain unchanged. This hypothesized lower publication cost; disassembly is mechanism evidence, not a speedup.

## Quick comparison: original FIFO → publication exchange

| Workload | Backend | Before | After | Time saved | Improvement % | ABBA / BAAB | CPU correctness |
|---|---|---:|---:|---:|---:|---:|---|
| 2 colliding targets | Vulkan | 0.551018 s | 0.554175 s | -0.003157 s | **-0.57%** | +0.57% / -0.95% | 8/8 PASS |
| 8 colliding targets | Vulkan | 0.591386 s | 0.597452 s | -0.006066 s | **-1.03%** | -2.08% / +0.31% | 8/8 PASS |
| 10 colliding targets (above capacity) | Vulkan | 1.257119 s | 1.165055 s | +0.092064 s | **+7.32%** | +6.32% / +2.55% | 8/8 PASS |
| 8 noncolliding targets (control) | Vulkan | 0.366341 s | 0.368188 s | -0.001847 s | **-0.50%** | -1.68% / +0.90% | 8/8 PASS |

Time is **fixed guest work**, seconds per 8,000,000 calls, not frame time or FPS. Each before/after value is the median of four attempt-level means; each attempt mean contains all ten guest samples. Each order has two A/two B attempts. Improvement %=100*(before-after)/before; positive is faster. All rows are the maintained `cpu_translation_blocks.` leaves; suffixes are `jump_cache_collision2`, `jump_cache_collision8`, `jump_cache_collision10`, `jump_cache_noncollision8`, respectively. Two/eight/ten deliberate same-slot targets and eight distinct-slot targets execute the same arithmetic per call. Eight is the victim capacity; ten attacks capacity overflow. Targets use byte offsets within one 4 KiB page, not separate pages.

The ten-target aggregate is favorable in both orders, **but not established as a patch benefit**. A ranges 1.051–1.300 s and B ranges 1.040–1.276 s. A4 is faster than B4; A2 is faster than B2. Both binaries exhibit overlapping fast/slow observations, and the sample mixture differs. No confidence bound or causal explanation is inferred from four attempts per binary. Smaller cases/control give opposing order signs. Both canonical campaign reports classify their outcome `inconclusive`.

### All candidate-comparison attempt means (seconds)

| Workload | A1 | A2 | A3 | A4 | B1 | B2 | B3 | B4 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 colliding targets | 0.562363 | 0.552115 | 0.549922 | 0.548032 | 0.551832 | 0.556272 | 0.552273 | 0.556078 |
| 8 colliding targets | 0.590315 | 0.599125 | 0.591065 | 0.591707 | 0.604879 | 0.609287 | 0.589033 | 0.590026 |
| 10 colliding targets (above capacity) | 1.262874 | 1.251364 | 1.300161 | 1.051278 | 1.078812 | 1.276461 | 1.040297 | 1.251297 |
| 8 noncolliding targets (control) | 0.366476 | 0.362081 | 0.372554 | 0.366206 | 0.369737 | 0.371038 | 0.365472 | 0.366640 |

The table labels logical attempts; physical order is **A1 B1 B2 A2 B3 A3 A4 B4**. Full raw ten-sample guest results, mean/median/min/max/p95 metrics, all run IDs and canonical JSON/Markdown/CSV comparisons are preserved in the ZIP. No sample was removed or rerun.

## Fresh A/A qualification

Both A and B use the exact original FIFO executable and identical frozen inputs. This campaign preceded the candidate comparison.

| Workload | Backend | Before | After | Time saved | Improvement % | ABBA / BAAB | CPU correctness |
|---|---|---:|---:|---:|---:|---:|---|
| 2 colliding targets | Vulkan | 0.552064 s | 0.553484 s | -0.001419 s | **-0.26%** | -0.59% / +2.53% | 8/8 PASS |
| 8 colliding targets | Vulkan | 0.606826 s | 0.596239 s | +0.010587 s | **+1.74%** | -0.31% / +22.51% | 8/8 PASS |
| 10 colliding targets (above capacity) | Vulkan | 1.259932 s | 1.266280 s | -0.006348 s | **-0.50%** | -0.45% / -5.98% | 8/8 PASS |
| 8 noncolliding targets (control) | Vulkan | 0.376230 s | 0.368337 s | +0.007893 s | **+2.10%** | -0.37% / +24.73% | 8/8 PASS |

These are observed apparent differences between identical binaries, **not improvements**. A4 eight-target/control means are 0.908/0.600 s; all remain included. ABBA/BAAB discrepancies and earlier diagnostic variation prevent declaring a stable noise floor. Passing correctness and comparison eligibility do not resolve that variation.

## Validation and cache state

- Fresh GCC 14/O2 i386-softmmu enabled build succeeds with the actual `qemu-system-i386` and helper targets. Fourteen helper cases pass locally and on Deck through the existing saved standalone definition, including 20,000-operation FIFO oracle, 10,000 delayed publications and guarded storage reuse. Read-only review reports no memory-ordering blocker. These checks do not prove arbitrary VM remapping or concurrent reclamation.
- Generated host/target/device configs, 18 dependency source trees and configure options match original FIFO build inputs. CPU dispatch object's `.text`, `.text.unlikely`, `.rodata`, `.rodata.str1.1` bytes match exactly. Whole executable layouts differ; this does not eliminate host-layout effects. No fresh disabled build is claimed for this experiment; changed code remains inside the existing default-off source guard.
- **16/16 clean measurement attempts** finish, pass all four independent arithmetic/page contracts, complete evidence and remain comparison-eligible. **4,960 measurement artifacts plus 21 helper artifacts collected**, no exclusions. All private Mesa disk namespaces begin empty, show driver writes and qualify with `mesa_private_disk_writes_observed`; no uncontrolled-cache waiver, purge or policy intervention. All operator-activity counters are zero.
- Native measurements use Deck `10.0.0.123`, Vulkan/RADV, 128 MiB; updated maintained runner `e17919c849b840d43171a615b288f559fbec9e4c`. Routine testing used LAN HTTP, no SSH. Deck returns idle. OS page cache, driver memory cache, power/delivered CPU frequency and scheduling remain limitations.
- Rendering/physical Xbox, other 159 XISO leaves, OpenGL, reached-game >=300-second PGR2, resources and actual VM lifecycle are **not qualified**. No new production-parent comparison was run after the inconclusive direct comparison. Do not subtract these percentages from earlier campaigns or claim net game gains.

## Immutable workload and product identities

- A runtime `5679cce099eb777977e761245de47e77d05d0c05`; application `i167-5679cce0-enabled`; executable SHA-256 `51cd99ef27b7212ba75029c489b31b36b44f732302cee27276c2c62756357c24`.
- B runtime `1beff07a535acf153bf774ec578d5146b0143d23`; application `i167-1beff07a-publish-mb`; executable SHA-256 `0c856d3a4c82c7b049a52bb7b3bec0079799d038d5983be97ced449d9e899fcc`. Original companions/config/EEPROM/library bytes unchanged; package manifests retained.
- Native helper application `i167-publish-helper-1bef-001`; executable SHA-256 `14c9a50da1d759fca5fe4f3c5222ed37540532ab154f0ce652710779502d6eef`; saved definition `i167-compact-fifo-reference-v1` @ `353c488b6c2ee752c550b32e30a68b561bf9b32ed72579c14346d3038c8a9136`.
- Retained genuine fixture [source draft #54](https://github.com/Mainkill1/xemu-perf-tests/pull/54), compiled source `19f252d79aed0c3614ec43fab1d468f09ff6296e`; ISO `87296b1b35e9f3ade67661ed53d42e9d11ff786bf9ecb07182149c9924c7767f`; catalog `sha256:5f579ca71a19d22c66ffa213aaa665983125bacb9c962bfd690798b7e3770012`.
- Suite `i167-collision-arithmetic-v1-suite` @ `a7cbe7d53b011dae8b9d7efd189cf62407fcd4414c72aabc27bdf0b5bc361171`; source-derived CPU reference `db086d21eb0e600f30246c1a8bc129290dcce0f16f4308ead9765f35cd49876b`. Suite remains administratively `unverified`; passing selected arithmetic contracts does not promote it to a full renderer baseline. No observed framebuffer/timing self-reference.
- Same explicit suite configuration: zero warmups, multiplier one, per_iteration. Campaigns `i167-publish-aa-20261003-001` and `i167-publish-vs-fifo-20261003-001`. Plans, selected requests and immutable revisions retained.

## Preserved failures and bounded handoff

Initial build command named nonexistent `xemu` target and failed before compilation; `build.log` preserves it. Correct target build log is separate. A headless `--version` query actually initialized GUI and failed due to unavailable local display; this is **not** a passed launch test. Existing third-party build warnings remain visible; no warning-free full-build claim. No emulator measurement attempt failed, was waived, retried or filtered.

The original FIFO and source draft #54 stay available. Keep #301 draft/HOLD; publication source is archived, excluded from owning product runtime. Future work must address above-capacity admission/search cost and characterize between-process variation before claiming a useful change. This pass adds no publication-only optimization to the product and does not close broader #167.

[All raw measurements and logs](EVIDENCE.zip) · [Artifact identities and public-byte exclusions](INDEX.json) · [Summary and every attempt](SUMMARY.json). No emulator results are stored in runner/test-source PRs. Executables, firmware/private state and duplicate diagnostics archives remain local with hashes recorded.
