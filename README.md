# ENG-2026-523 DSP-before-Issue42 release candidate

This branch is the clean dependency-ordered candidate for the DSP snapshot
resume repair and Issue 42 Vulkan bordered-texture copy-extent repair. It is a
correctness candidate, not a performance optimization. A fully passing result
is intended to become part of the stable correctness parent from which later
Full-Speed performance candidates are extracted one at a time.

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

The exact production/test order for this branch is:

```text
8d272609baa060baaea48971a45ea3a48762b65c  report correctness base
    |
d6cd4fe10ae85bf35f375bc3036c5caaf0e6de26  quiesce DSP before snapshot VMState load
    |
1d0488ac46bbbd8b44f0fdaa08a33bc130ea7a74  Issue 42 Vulkan copy-extent repair (production head)
    |
62f1f95043010f583dcc153cd267c1b0c9d83974  opt-in XISO markers
0656c3c0e76c054235611d1aa9a6382c64f45939  opt-in flip probe
e29a3b9c597066b6badacbbcc5152c273b4cca47  release-safe flip telemetry
5b46cbfb31af58ece9df20b3787c8f40a783ea82  opt-in retail telemetry (tested source)
```

The four commits after the production head are measurement-only. Build and
test manifests must name both `production_head` and `source_commit` so nobody
mistakes instrumentation for production behavior.

## Release test contract

The foundation and every isolated candidate use the same test inputs and xemu
configuration:

1. Full 149-record Mainkill1's Test Suite XISO on OpenGL, including the normal
   10-second splash/autostart, in two independent Release runs.
2. The same 149-record XISO on Vulkan in two independent Release runs, with
   Vulkan validation enabled and zero VUIDs required.
3. Morrowind heavy snapshot `vm-20260903021051`, one 60-second Release run per
   renderer.
4. PGR2 true fresh boot, one 60-second Release run per renderer, with a
   10-second BIOS allowance followed by
   `A-3,A-10,A-2,A-2,F-2,A-2,A-2,A-2,A-2,A-2,A-7`.
5. Backend-separated FPS, p95, p99, CPU, RAM, process GPU,
   dedicated/shared GPU memory, device GPU, and resident VRAM results.

Debug builds and a third XISO repetition are diagnostic tools only. They are
not part of the routine release/performance campaign.

Correctness/safety changes must pass the XISO and retail functional gates
without regressing the common baseline. A performance candidate must also show
a positive, workload-attributable result in Morrowind and/or PGR2. Small wins
are retained when repeatable; neutral or negative candidates remain research.

## Reproducible Windows builds

The qualified build was produced on the dedicated Linux build host
`10.0.100.1` from a clean detached checkout of the tested source commit. The
only required external compiler environment is the public xemu Windows
cross-toolchain image from GitHub Container Registry, pinned by digest:

```text
ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2
```

The image supplies GCC 16.1.0 and the static MXE target
`x86_64-w64-mingw32.static`. No private compiler or SDK patch is used. The
build host installs `curl` inside the ephemeral container because xemu's DSP
fallback downloads a pinned input during configuration.

Retrieve the exact image without relying on a mutable tag:

```bash
docker pull ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2
```

Release/full-LTO:

```bash
./build.sh -j32 -p win64-cross \
  --extra-cflags="-flto-incremental=/xemu-cache/lto -flto-partition=cache" \
  -Db_lto=true -Dx86_version=3
```

Optional diagnostic Debug/assertions build (not used for routine timing):

```bash
./build.sh --debug -j32 -p win64-cross -Db_lto=false -Dx86_version=3
```

Set `CROSSPREFIX=x86_64-w64-mingw32.static-` and
`CROSSAR=x86_64-w64-mingw32.static-gcc-ar` for both builds. Record the exact
source SHA, clean-tree result, container digest, full command, build log, and
SHA-256 of `xemu.exe`, `LICENSE.txt`, and the matching symbol artifacts.

For the qualified Release package, `BUILD_JOBS=32`, `CCACHE_MAXSIZE=512M`, and
the incremental LTO cache was mounted at `/xemu-cache/lto`. The resulting
`xemu.exe` SHA-256 is
`9589dc107db324e14bf55045dd6c5c439ed6bf4de254d42ea42f73882002cbee`.
Run `sha256sum -c SHA256SUMS.txt` in the package directory before testing.

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

This correctness child resolves Forgejo issue #39. GL and Vulkan report queues
now retain the resolved DMA descriptor at GET_REPORT time rather than only its
mutable RAMIN address. Report publication validates the complete 16-byte record
against the descriptor's inclusive limit and the final masked VRAM range using
subtraction-based checks. Invalid guest ranges are rejected and logged instead
of relying on assertions or writing beyond mapped VRAM.

This branch adds no performance candidate. It must become part of the common
correctness foundation before any per-feature A/B is release-valid.

## DSP snapshot-resume repair

Snapshot VMState load now quiesces the DSP worker before restoring DSP memory
and registers, then resumes it after the load completes. This prevents the
worker from executing a partially restored PRAM image. The repair preserves
the required BQL-to-DSP lock order; the superseded first attempt that waited
while holding the BQL is not part of this branch.

This is a prerequisite correctness fix for Issue 42 validation. The Issue 42
binary-layout change exposed the pre-existing race, but the Vulkan-only texture
change did not directly cause DSP corruption. A full WER dump captured the DSP
worker executing the `0xCACACACA` restored-memory sentinel, and the corrected
quiesce order removed that failure from the repeated snapshot gate.

## Issue 42 bordered decoded-BC2 extent repair

Vulkan texture layout, image allocation, staging size, and image-copy extent
now use one shared physical storage extent for bordered swizzled textures.
The guest's logical texture dimensions remain unchanged. This removes the
validation error caused when decoded BC2 border storage was larger than the
destination image extent.

```text
guest logical texture shape
          |
          v
derive NV2A physical border-storage extent once
          |
          +--> decode/staging dimensions
          +--> VkImage allocation dimensions
          +--> VkBufferImageCopy extent
```

The patch is deliberately local to Vulkan texture storage. OpenGL is retained
as a negative control, and native compressed-texture work remains a separate
later performance candidate.

## Qualified Release results

The exact executable identified above passed the entire current Release gate
in the visible Windows Session 1 GUI on `10.0.7.1`. NVIDIA profile setup was
disabled and no host-load timing option was enabled.

| Backend | XISO run 1 | XISO run 2 | Functional hashes | Vulkan VUIDs |
|---|---:|---:|---:|---:|
| OpenGL | 149/149 PASS | 149/149 PASS | PASS | N/A |
| Vulkan | 149/149 PASS | 149/149 PASS | PASS | 0 |

| Workload | Backend | FPS | p95 | p99 | Result |
|---|---|---:|---:|---:|---|
| Morrowind heavy snapshot | OpenGL | 35.616 | 33.831 ms | 49.998 ms | PASS |
| PGR2 true fresh boot | OpenGL | 22.954 | 64.056 ms | 68.865 ms | PASS |
| Morrowind heavy snapshot | Vulkan | 15.506 | 66.946 ms | 83.188 ms | PASS |
| PGR2 true fresh boot | Vulkan | 16.310 | 82.248 ms | 88.154 ms | PASS |

Both retail captures ran for 60 measured seconds after their documented
warmup. Screenshots were inspected for the intended playable state, PGR2's
complete input log was verified, source ownership matched the executable, and
all four traces reported zero ETW lost events and buffers.

The matched-parent Vulkan XISO control containing the DSP repair but not Issue
42 stopped after 35 boundaries in 2/2 runs. Adding Issue 42 changed that result
to 149/149 in 2/2 runs with zero VUIDs. That deterministic boundary transition
is the primary correctness proof.

## A/B performance and resource analysis

The isolated comparison is F1 (report foundation + DSP repair) to F2 (F1 +
Issue 42). Positive FPS is faster; negative p95/p99 is better.

| Workload | Backend | FPS delta | p95 delta | p99 delta |
|---|---|---:|---:|---:|
| Morrowind heavy snapshot | OpenGL | -0.00% | -2.20% | -1.29% |
| PGR2 true fresh boot | OpenGL | -2.53% | +3.01% | +1.42% |
| Morrowind heavy snapshot | Vulkan | -4.77% | -0.28% | -0.18% |
| PGR2 true fresh boot | Vulkan | -0.01% | +0.83% | +2.04% |

The code change is Vulkan-only, so the OpenGL movement is a negative-control
estimate of run variance. PGR2 Vulkan is unchanged. Morrowind Vulkan's flip
rate moved down while p95, p99, and stall count improved, so the one retail
sample does not support a causal speed regression or improvement. This branch
is accepted as a performance-neutral correctness repair.

CPU, working set, private bytes, process GPU, dedicated/shared GPU memory,
device GPU, and resident VRAM were sampled throughout every lane. F1-to-F2
private-memory peaks changed by -0.47% to +0.25%, dedicated-memory peaks by
0.00% to +0.96%, and VRAM peaks by -1.84% to 0.00%. No sustained resource
spike or unbounded lifetime growth was observed. The complete mean/peak table,
including every small signed delta and retained transient counter warning, is
in `performance-analysis.md` in the validation evidence bundle.

The first frozen upstream PGR2 cells captured a different pacing state and are
not used for per-patch attribution. A final upstream rerun remains queued, as
does the matched pre-DSP foundation campaign needed to isolate the DSP repair.
Those follow-ups do not weaken the deterministic Issue 42 correctness result.

## Evidence identity

The accepted evidence root is
`active/release-validation/evidence/2026-09-04/clean-dsp-before-issue42` in the
lab working set. Important SHA-256 identities are:

- OpenGL worker report:
  `4d5af2fee0089c8cee0a6083f31edf7a376f90c1cd2f5169153b9477cf429155`;
- Vulkan worker report:
  `f7c1ebd22e2335e751ee61b617c88e6256e6458af7075331c4134a0fd8dfdb17`;
- Vulkan canonical 100-file Windows tree:
  `839ab69c8a4e05607a5b2ce4334d0988522c7bdd22de9dc41930fb78f23274ab`;
- Vulkan J-mirror aggregate:
  `5aa52f040e87b9f7c21c21caf7efa7465780c80b1773d8ffdbeac0480148a1a3`;
- full performance analysis:
  `8de61ec46a76ff78b3466d16f0d112a88a98846fd962d18f37835a03ff6b3bc6`.

The release contains the exact qualified build package and a compact evidence
bundle. Large ETL files remain in the hashed lab evidence tree and are not
duplicated in the public release asset.
