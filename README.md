# ENG-2026-523 release correctness foundation

This branch is the common correctness parent for isolated Full-Speed
performance candidates. It intentionally contains no performance candidate.
Every performance branch used for release attribution must fork the exact
production commit recorded below and contain only one documented candidate
delta (including any correctness fix required by that candidate).

The foundation starts at upstream xemu
`d73326b62199c6dd952ef512947710e1333a49d3` and contains:

- Vulkan shader-demote feature enablement and validation-layer corrections;
- serialization of queued Vulkan texture overwrites;
- PTIMER post-load, zero-ratio, and overdue-alarm reconciliation;
- the Windows SDL clipboard/window construction guard;
- queued report DMA ownership; and
- synchronous Vulkan report publication at the guest-visible GET boundary.

The opt-in live XISO marker harness is applied only to test builds. It is not
part of this production branch and does not alter normal emulator behavior.

## Release test contract

The foundation and every isolated candidate use the same test inputs and xemu
configuration:

1. Full 147-record perf-lab XISO, including normal 10-second splash/autostart,
   three independent Release runs and three independent Debug runs.
2. Morrowind heavy snapshot, one 60-second Release run and one 60-second Debug
   run.
3. PGR2 fresh boot, one 60-second Release run and one 60-second Debug run,
   with a 10-second BIOS allowance followed by the recorded input sequence.
4. CSV output containing signed FPS change and p95/p99 frame times.

Correctness/safety changes must pass the XISO and retail functional gates
without regressing the common baseline. A performance candidate must also show
a positive, workload-attributable result in Morrowind and/or PGR2. Small wins
are retained when repeatable; neutral or negative candidates remain research.

## Reproducible Windows builds

Build both configurations from a clean checkout of the exact commit with the
public xemu Windows cross-toolchain image pinned by digest:

```text
ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2
```

The image supplies GCC 16.1.0 and the static MXE target
`x86_64-w64-mingw32.static`. No private compiler or SDK patch is used. The
build host installs `curl` inside the ephemeral container because xemu's DSP
fallback downloads a pinned input during configuration.

Release/full-LTO:

```bash
./build.sh -j32 -p win64-cross \
  --extra-cflags="-flto-incremental=/xemu-cache/lto -flto-partition=cache" \
  -Db_lto=true -Dx86_version=3
```

Debug/assertions:

```bash
./build.sh --debug -j32 -p win64-cross -Db_lto=false -Dx86_version=3
```

Set `CROSSPREFIX=x86_64-w64-mingw32.static-` and
`CROSSAR=x86_64-w64-mingw32.static-gcc-ar` for both builds. Record the exact
source SHA, clean-tree result, container digest, full command, build log, and
SHA-256 of `xemu.exe`, `LICENSE.txt`, and the matching symbol artifacts.

General project information is available at [xemu.app](https://xemu.app).

## This candidate: ordered vertex staging

This branch is the one-variable B for common foundation
`60464efb92c9e39028cd825a43ba361ec59a9476`. It replaces repeated synchronous
`VERTEX_BUFFER_DIRTY` finishes with command-buffer-ordered copies from a
persistently mapped, bounded vertex staging allocation into the device-visible
vertex buffer. Direct writes remain for idle/reset paths. If the staging range
is exhausted, xemu completes the active submission before reuse; the allocation
can grow only from 4/8 MiB to a hard 16 MiB cap.

No wait telemetry, texture staging, submission batching, TCG change, or other
performance candidate is included in the production delta. The same opt-in
retail telemetry and live XISO marker overlay will be applied to A and B only
for measurement.
