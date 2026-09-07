# PR 15 qualification recipe

## Build

Build the exact parent and candidate commits with the pinned MinGW GCC image in `MANIFEST.md`. Build the candidate in Release with full LTO and x86-64-v3, and in Debug. Record commit, tree, executable hash, build options, and a clean source status.

## Focused cubemap oracle

Run BC1, BC2, and BC3 bordered cubemaps. For each format, give all six faces distinct colors and sample four mip levels. Record all 24 face/mip samples, the final framebuffer hash, Vulkan validation messages, and process exit.

The negative control at the parent must reproduce six copy-extent VUIDs per format and device loss. The candidate passes only when all samples match, the framebuffer hash is `6a84a3194415b625`, and Vulkan reports zero VUIDs in both Release and Debug.

## XISO suites

Run the 152-record suite twice on Vulkan and twice on OpenGL. Require exact record identity, every functional hash, clean process exit, and zero Vulkan VUIDs. Preserve every row in `xiso-new-results.csv`.

Run the matched 147-record suite on both parent and candidate, twice per renderer. Treat its timing as context because live markers are unavailable. Preserve all matched rows in `xiso-comparison.csv`.

## Retail checks

For Morrowind, load the fixed snapshot, wait five seconds after the emulator reports running, send Start, wait two seconds, send B, wait two seconds, and measure for ten seconds.

For PGR2, use a fresh private disk for every cell and run parent, candidate, candidate, parent for each renderer. Measure for 60 seconds. Record average guest FPS, p95/p99 guest intervals, host presents, CPU, GPU, and memory.

Require guest progression, validated final images, an unchanged seed, successful private-disk cleanup, and no remaining owned collector or emulator process.
