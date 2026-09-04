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

1. Full 149-record perf-lab XISO, including normal 10-second splash/autostart,
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

## Report finish attribution and Vulkan result validation

This correctness child resolves Forgejo issue #40. Vulkan GET_REPORT and the
idle pending-report drain use a dedicated `REPORT` finish/profile reason rather
than the generic `STALLED` bucket. Fence reset and query-result return values
are checked before submission or result consumption.

The synchronous report boundary is retained for correctness. Its cost must be
reported separately in the later per-feature retail campaign; this commit does
not claim that the wait is free or that it should be removed.

## Report DMA snapshot and bounds repair

This correctness child addresses the two original blockers in Forgejo issue
#39. GL and Vulkan report queues
now retain the resolved DMA descriptor at GET_REPORT time rather than only its
mutable RAMIN address. Report publication validates the complete 16-byte record
against the descriptor's inclusive limit and the final masked VRAM range using
subtraction-based checks. Invalid guest ranges are rejected and logged instead
of relying on assertions or writing beyond mapped VRAM.

This branch adds no performance candidate. It must become part of the common
correctness foundation before any per-feature A/B is release-valid. Release
promotion also requires a checked complete RAMIN descriptor load and explicit
DMA class/target validation; those remaining hardening gates stay tracked on
issue #39.

## NVIDIA preferred-P-state migration

This correctness child resolves Forgejo issue #41. The Windows NVIDIA profile
setup performs a versioned, one-time migration. It queries the xemu profile's
legacy preferred-P-state setting and deletes it only when the value is the
previously forced `Prefer maximum performance` value and the setting is owned
by the current xemu profile. Inherited global/default policy is not changed.

The migration version is saved to `xemu.toml` only after the NVIDIA profile
transaction succeeds. Later user changes are therefore left alone on normal
startup. Launching with NVIDIA profile setup disabled performs no migration or
other driver-profile writes.

`NvAPI_DRS_GetSetting` has three explicit outcomes. Success inspects the
current-profile value, official `NVAPI_SETTING_NOT_FOUND` (`-160`) confirms
that no migration is needed, and every other status aborts the transaction so
version 1 is not consumed and the next launch retries. This closes the
failure-path defect found during external branch review.

Exact commit `cab67c70daf2141a7657565036fe026cd9f551d1` builds cleanly with
the pinned GCC 16.1/MXE commands above. Artifact SHA-256 values are:

```text
Release  986e87224f5ce4b675d4f8cf96a0382e536629afb1524fc484b406eacd6fd1b4
Debug    ed21a9b198e4dffaa3348cc5da768c3c190f3da1d884e3d73fcc4d2a081a4733
```

Both builds passed injected-host failure tests on Windows 10.0.7.1: status
`-1` logged the query failure and preserved migration version 0, while status
`-160` saved the profile and migration version 1. The complete 149-record XISO
gate is not claimed here because the shared correctness foundation currently
hits decoded-BC2 device-loss issue #42; this branch remains focused-validated
and globally release-blocked until that common defect is repaired.
