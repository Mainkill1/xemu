# Cold-handler branch-hint experiment — rejected

This bounded authoring-host experiment tests one compiler-layout hypothesis for the reported dispatch regressions. **A is the existing #281 typed-handler candidate at `58df82f`; B adds `unlikely()` to its cache-miss branch.** Both are rebuilt from the same copied source pathname and matching compiler arguments. The original previous-main native comparison continues with unchanged product code.

Units are median **ns per emulated instruction** over five-million-instruction batches. Positive improvement is `100*(A-B)/A`. Each compiler ran six replay lanes × AAAA/ABBA/BAAB × eight four-launch blocks = **576 attempts**; each balanced row has 16 A and 16 B observations. All 1,152 attempts exited zero and each lane's architectural digests matched. The hinted build passed all ten existing production-boundary checks on both compilers.

| Compiler | Replay | ABBA existing | ABBA hinted | Improvement | BAAB existing | BAAB hinted | Improvement |
|---|---|---:|---:|---:|---:|---:|---:|
| gcc | Normal only | 3.6663 | 3.4683 | +5.40% | 3.6652 | 3.4689 | +5.36% |
| gcc | Parallel only | 3.8857 | 3.6996 | +4.79% | 3.8847 | 3.7022 | +4.70% |
| gcc | Mixed | 3.8075 | 3.6196 | +4.93% | 3.8072 | 3.6196 | +4.93% |
| gcc | 4,096-slot working set | 3.8074 | 3.6174 | +4.99% | 3.8095 | 3.6200 | +4.97% |
| gcc | Clear cache every 4,096 instructions | 4.5477 | 4.8266 | -6.13% | 4.5225 | 4.8151 | -6.47% |
| gcc | Identical-word P write each instruction | 4.7051 | 4.9862 | -5.97% | 4.6974 | 4.9864 | -6.15% |
| clang | Normal only | 2.8222 | 2.7627 | +2.11% | 2.8178 | 2.7624 | +1.97% |
| clang | Parallel only | 3.0305 | 3.0175 | +0.43% | 3.0560 | 3.0281 | +0.91% |
| clang | Mixed | 3.1754 | 3.1141 | +1.93% | 3.1306 | 3.1266 | +0.13% |
| clang | 4,096-slot working set | 3.1397 | 3.1186 | +0.67% | 3.1185 | 3.0823 | +1.16% |
| clang | Clear cache every 4,096 instructions | 3.7134 | 12.0491 | -224.48% | 3.7113 | 12.1735 | -228.01% |
| clang | Identical-word P write each instruction | 3.8950 | 4.3522 | -11.74% | 3.9621 | 4.3323 | -9.34% |

**Decision:** retain the result and do not apply the hint to the product. Warm GCC lanes improve approximately 4.7–5.4%, but cold/update lanes regress approximately 6%. Clang warm gains are smaller and order-sensitive; its cold cost rises from roughly 3.71 to 12.05–12.17 ns/instruction, and update cost rises 9.3–11.7%. This does not resolve the existing first-use/write-heavy tradeoffs. It has no native xemu/game qualification and is not a measured remedy against the original main baseline.

Exact disassembly shows rearranged dispatch blocks in both compilers. GCC's dispatch extent is unchanged at 2,077 bytes; Clang grows from 1,780 to 1,789 bytes. `.text` hashes differ. These observations establish a code-layout effect, not the precise hardware cause of every timing change; no PMU attribution was collected.

GCC 14.2/O2/LTO and Clang 19.1.7/O2/ThinLTO/LLD use the retained fixture and existing QEMU utility archive. CPU 0 is pinned. Frequency, power and shared activity are uncontrolled; all AAAA controls and full ranges remain in `records.tar.gz`. Compiler campaigns run sequentially. Cold time includes cache clearing; the write lane writes identical words. These are externally selected-PC replays of two instruction bodies, without PCM or guest game execution.

The archive retains both source snapshots with their original copyright/license notices, the one-line patch, compile argv/logs, unit output, all attempts, manifests, ranges and exact dispatch disassembly. Executables remain built locally with identities recorded in `manifest.json`; no emulator/firmware/disk images are redistributed. Reproduce with the existing `tests/unit/dsp-dispatch-benchmark.py` and the recorded matching builds, using a fresh output directory for each campaign. Raw archive payload hashes were reread and verified.
