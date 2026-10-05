# PR312 refinement, 2026-10-05

Source parent: afdde9eb62accd07686be104ebf4a9381785aaab. Candidate: e428a42bc3e0f2f60caccc9b738bde47e7a2f800. Baseline helper: 4aa11b5d90029c3e46eee8382d80be0abb3635a9.

The previous correctness reproduction remains in this directory. The new helper starts at1024 and halves toward the device limit, then uses a low-bit divisibility test. The corrected exact-header check matches the previous PR helper in3,146,496 cases on each compiler; the initial shadowed-header run is inapplicable (see NATIVE-RESULTS.md). The production surface fixture passes16/16 strict and ASan/UBSan; LeakSanitizer initially fails because this environment uses ptrace, and the successful rerun disables only leak detection. The full production surface-compute.c compiles with warnings as errors. No shader, geometry, barriers, queue synchronization or gameplay procedures are changed by this refinement.

## Isolated helper cost

Twenty million runtime calls per cell, one executable per compiler, ABBA thenBAAB. Each cell consumes a checksum; all checksums agree between algorithms for each case. Values are medians of four cells per side, nanoseconds per helper call on the local AMD/Linux host. Two monotonic clock reads per batch, no inner-loop validation. This measures the selector only and is not FPS or GPU execution evidence. Native title and physical Deck conversion results are in NATIVE-RESULTS.md; XISO has its own campaign completion records. This original table is the copied-policy probe, not the corrected exact-production-selector run.

| Compiler | Case | Previous ns | New ns | Improvement |
|---|---|---:|---:|---:|
| gcc | 1024 | 2.535 | 0.861 | +66.05% |
| gcc | 256 | 2.429 | 1.197 | +50.74% |
| gcc | 192 | 2.321 | 1.379 | +40.61% |
| gcc | odd | 13.653 | 3.260 | +76.12% |
| gcc | partial | 4.208 | 1.236 | +70.62% |
| clang-19 | 1024 | 2.811 | 1.113 | +60.40% |
| clang-19 | 256 | 2.394 | 1.288 | +46.23% |
| clang-19 | 192 | 2.261 | 1.518 | +32.87% |
| clang-19 | odd | 4.725 | 3.056 | +35.32% |
| clang-19 | partial | 2.784 | 1.383 | +50.31% |

## Production adapter Vulkan execution

The retained native fixture links the actual candidate surface-compute.c object and actual renderer/PGRAPH headers. It executes32 pack/unpack commands through the real descriptor pool, pipeline cache, pipeline creation, binding, push constants and Vulkan dispatch. All destination depth/stencil pixels are checked against independent CPU expectations for D24 and D32, scale1/2, ordinary1024 limits, X256, invocation128 and non-power X192. Both zero-output functions return without descriptor writes or shader compilation.

Clean-main production surface-compute.c, linked against the same fixture, passes its first8 ordinary-limit controls and then times out after3s at the first X256 conversion. Candidate completes all32 conversions. Original source is extracted unmodified from afdde9eb62.

Device is local Mesa llvmpipe, LLVM19.1.7, software Vulkan, actual X/inv limits1024. Restricted fixture limits only decrease those properties; they are not claims about the actual device limits. The same adapter fixture later passed32/32 on the physical Steam Deck AMD RADV GPU; Windows physical fixture execution is not established. See native/deck-compute-result.json. Shader-module compilation uses shaderc in the fixture instead of xemu's GLSL compiler; optional debug markers are no-ops and the key hash uses fixture FNV. Production compute selection, cache keys, GLSL, resource descriptors, dispatch and output buffers are exercised. No guest program/texture bytes are involved. This is an adapter correctness result, not whole-renderer ownership or performance evidence.
