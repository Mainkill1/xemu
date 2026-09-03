# Full Speed

`Full-Speed` is the consolidated performance branch for the xemu changes
reviewed after the original Full-Speed baseline (`6234f3606f`). Source
branches remain open: this branch contains the selected production changes,
not replacements for their history.

## Included branches

| Branch | Status in Full-Speed | Change |
| --- | --- | --- |
| `feature/eng-2026-520-perf-etw-event-telemetry` | Included | Adds opt-in NV2A retail-capture telemetry so CPU, OpenGL, and Vulkan work can be aligned with ETW captures without changing normal runtime behavior. |
| `feature/eng-2026-525-vk-wait-telemetry-u524a4d5b` | Superseded by v2 | Introduced per-reason Vulkan submit/wait attribution. Its functional changes are retained through the v2 branch. |
| `feature/eng-2026-525-vk-wait-telemetry-v2` | Included | Reduces telemetry overhead by timing the first occurrences and periodic hot occurrences of each Vulkan wait reason. |
| `feature/eng-2026-523-vk-vertex-upload-staging` | Included | Stages ordered vertex-RAM updates in persistent Vulkan buffers, avoiding unnecessary finish/wait work while retaining bounded capacity behavior. |
| `feature/eng-2026-523-vk-cpu-hotpath-attribution` | Included | Attributes Vulkan draw-preparation CPU work in the opt-in telemetry output. |
| `feature/eng-2026-523-win-qpc-clock-fastpath` | Included | Avoids an unnecessary 128-bit divide in exact Windows QPC scaling. |
| `feature/eng-2026-523-win-nvidia-prefer-max-performance-u90829d4e` | Included | Requests NVIDIA's maximum-performance preferred power state for the xemu profile on Windows. |
| `feature/eng-2026-523-tcg-active-mmu-dirty-reset-ud9e1e15a` | Included | Skips empty MMU modes while resetting code-dirty state. |
| `feature/eng-2026-523-tcg-x86-static-state-tb-lookup-u3121717e` | Included | Final stacked TCG/Vulkan rollup: reuses known x86 translation state, specializes dirty-memory paths, avoids redundant register clearing, stages texture uploads in the ordered draw stream, targets surface callbacks directly, and clears pipeline-change state after binding. |
| `feature/eng-2026-523-vk-native-bc-on-vertex-staging-u8a09a4ea` | Included, conflict-resolved | Uploads supported BC1/BC2/BC3 textures in native Vulkan block-compressed formats rather than decoding them on the CPU; unsupported layouts retain the decoded fallback. It was merged onto the later ordered texture-upload path and includes BC layout unit coverage. |
| `xemu-pr-staging: feature/eng-2026-336-stage-local-vulkan-uniforms-v3-uec89c153` | Included, conflict-resolved | Separates vertex- and pixel-shader uniform source generations so unchanged stages do not rewrite or re-upload their UBOs. It preserves exact float bit patterns, tracks effective polygon-offset inputs, and retains Full-Speed's existing VMState-compatible dirty-row handling. |
| `fix/eng-2026-523-vk-report-dma-ownership` | Included on this branch | Captures the active DMA report context when `GET_REPORT` is queued, so delayed Vulkan report publication cannot be redirected by a later context switch. |
| `feature/eng-2026-523-vk-texture-pipeline-fastpath-uaad84ed1` | Included on this candidate branch | Avoids rebuilding and looking up an unchanged Vulkan `PipelineKey` when only texture image/sampler descriptor identity changed. Shader-affecting texture state remains covered by `ShaderState`; descriptor refresh remains unchanged. |
| `fix/eng-2026-523-nv2a-ptimer-overdue-catchup-u76925787` | Included on this candidate branch | Advances an overdue NV2A PTIMER alarm directly to its next future epoch, avoiding a repeated immediate-timer/BQL storm after fresh game load while preserving the single hardware pending bit. |

### Build policy by branch type

The rows above are source-history and review boundaries, not different compiler
products. Every **included production branch** is compiled and distributed with
the same pinned x86-64 GCC/MXE release recipe below after it is reconciled onto
the exact Full-Speed commit. No included branch requires a private compiler,
local SDK patch, or branch-specific compiler flag.

| Branch type | Permitted build | What it proves |
| --- | --- | --- |
| Included production branch or Full-Speed rollup | Clean exact commit, pinned container digest, `build.sh`, LTO, and `-Dx86_version=3` as documented below | Release-equivalent correctness and performance evidence; eligible for sharing |
| Telemetry/instrumentation branch | The production recipe for final measurements; a non-LTO build may additionally be retained for symbol ownership | Production timing only comes from the LTO build; non-LTO timing is diagnostic |
| Research or rejected experiment | Incremental `ninja` is permitted for quick attribution, followed by the production recipe only if the change becomes a candidate | Incremental output is diagnostic and is not a distributable Full-Speed build |
| Bug investigation for the host-load option | One diagnostic binary with `display.window.reduce_host_cpu_usage` toggled at runtime | Attribution only; the option is known-bugged and must remain disabled in ordinary performance/correctness runs |

The perf-lab XISO is a separate Xbox guest project built with its own pinned
NXDK environment. It validates the emulator binary but is not an input to the
xemu Windows build. Record its source commit and ISO/catalog SHA-256 beside each
xemu build so another tester can reproduce the same broad gate.

## Reproducing Windows builds

The authoritative Windows build recipe is
[`.github/workflows/build-windows.yml`](.github/workflows/build-windows.yml).
`Full-Speed` does not require a private compiler or a locally patched SDK. The
x86-64 release build uses xemu's public, pinned GCC/MXE container from the
GitHub Container Registry and the repository's `build.sh` entry point.

### Required source identity

Build from a clean checkout of the exact commit being evaluated. Replace the
URL below with any Git remote that publishes this branch.

```bash
git clone --recurse-submodules <xemu-repository-url> xemu-full-speed
cd xemu-full-speed
git fetch --all --tags
git checkout <full-40-character-commit-id>
git submodule update --init --recursive
git status --porcelain=v1
git rev-parse HEAD
```

`git status --porcelain=v1` must print nothing. Record the `rev-parse` result in
the build manifest. A binary built from copied files over a different checkout
is useful for a quick diagnostic, but it is not a reproducible or
release-eligible Full-Speed build even when those copied files have matching
hashes.

### Pinned x86-64 toolchain

The release-equivalent toolchain used by this branch is:

| Component | Pinned value |
| --- | --- |
| Container | `ghcr.io/xemu-project/xemu-win64-toolchain-gcc:sha-2881edd` |
| Immutable digest | `sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2` |
| Base image | Ubuntu 24.04 |
| MXE source | `https://github.com/mxe/mxe.git` at `d9441093aa48e376aa6e49bcc7118ccc2b683a1e` |
| Target | `x86_64-w64-mingw32.static` |
| Compiler | GCC 16.1.0 |
| Linker | GNU binutils 2.46.0.20260210 |
| Meson in container | 1.11.1 |
| Meson selected by xemu's build venv | 1.9.0 |
| Ninja | 1.13.2 |
| Python | 3.12.3 |
| ccache | 3.6 |

These values come from the pinned image and
[`ubuntu-win64-cross/gcc.Dockerfile`](ubuntu-win64-cross/gcc.Dockerfile), not
from packages installed on the build host. Pulling by digest prevents a tag
from silently resolving to a different toolchain:

```bash
docker pull \
  ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2
```

### Release-equivalent x86-64 command

Run the following from the clean source root on a Linux Docker host. The LTO
options intentionally match xemu's recommended x86-64 release CI job. Cache
directories affect build time only and may be deleted between builds.

```bash
mkdir -p .build-cache/ccache .build-cache/lto

docker run --rm \
  -e CROSSPREFIX=x86_64-w64-mingw32.static- \
  -e CROSSAR=x86_64-w64-mingw32.static-gcc-ar \
  -e CCACHE_DIR=/xemu-cache/ccache \
  -e CCACHE_MAXSIZE=512M \
  -e LTO_CACHE_DIR=/xemu-cache/lto \
  -v "$PWD:/src" \
  -v "$PWD/.build-cache:/xemu-cache" \
  -w /src \
  ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2 \
  bash -lc 'apt-get update && apt-get install -qy curl && \
    mkdir -p "$CCACHE_DIR" "$LTO_CACHE_DIR" && \
    ./build.sh -j"$(nproc)" -p win64-cross \
      --extra-cflags="-flto-incremental=$LTO_CACHE_DIR -flto-partition=cache" \
      -Db_lto=true -Dx86_version=3'
```

The `curl` installation is part of xemu's official workflow and is required:
the pinned toolchain image itself does not provide an executable `curl`, while
the DSP fallback downloads its versioned binary during configuration.

The unpackaged executable is `build/qemu-system-i386w.exe`. `build.sh` also
creates the distributable `dist/xemu.exe` and `dist/LICENSE.txt`. Preserve
`build.log`; it contains the effective configure and compiler invocation.
The first direct-checkout build needs network access for sources declared by
the repository's pinned Meson wraps (for example SDL); subsequent builds can
reuse the populated source/build tree. Release CI instead consumes the source
archive produced by xemu's archive workflow, which pre-collects build inputs.

For a completely clean confirmation, use a fresh clone or remove only that
clone's `build`, `dist`, and `.build-cache` directories before rerunning. Do
not compare hashes from two builds unless source commit, container digest,
options, and generated version inputs are identical; timestamps and version
metadata can otherwise make byte-for-byte output differ.

On a rootless Docker installation, verify that the remapped container user can
write the source's generated `build`, `dist`, and `subprojects` paths plus the
external cache. If a disposable clean clone appears as `nobody:nogroup` inside
the container, grant write permission only to that disposable clone and cache
(the lab used `chmod -R a+rwX` on those two build-only trees). Do not apply that
workaround to a shared source checkout. Rootful Docker normally needs no
permission adjustment.

### Lab incremental builds versus recommended builds

The performance lab sometimes rebuilds only `qemu-system-i386w.exe` with
`ninja` in an already configured directory. That is appropriate for rapid
correctness diagnosis when the configure state is known and recorded:

```bash
ninja -C build qemu-system-i386w.exe
```

It does not run `build.sh` packaging, does not prove a clean source identity,
and must not be presented as the release-equivalent artifact when source files
were copied into another checkout. Final performance evidence and binaries for
distribution must be rebuilt from a clean exact commit with the pinned command
above.

### Windows PDB generation

GCC emits DWARF information in the original executable. The official workflow
then downloads `cv2pdb` 0.52 from
`https://github.com/rainers/cv2pdb/releases/download/v0.52/cv2pdb-0.52.zip`
and runs the following on Windows:

```powershell
cv2pdb64.exe xemu.exe
```

`cv2pdb` creates `xemu.pdb` and updates/strips the executable. Keep all three
symbol artifacts when collecting ETW evidence: the original DWARF-bearing
executable, the post-`cv2pdb` executable, and the matching PDB. Record SHA-256
hashes for each; never resolve an address with symbols from another build.

### Minimum build manifest

Every shared Full-Speed binary should be accompanied by:

```text
source commit (40 characters)
git status / clean-source result
container image tag and immutable digest
compiler, linker, Meson and Ninja versions
complete build command and options
LTO enabled/disabled
build.log
SHA-256 of xemu.exe
SHA-256 of the original DWARF executable, PDB and post-cv2pdb executable
host OS and architecture
```

The ARM64 job is a separate LLVM-based path pinned in
`.github/workflows/build-windows.yml`; do not substitute it for this branch's
x86-64 GCC performance binaries.

### Verified lab reproduction

The command above was independently exercised on the Linux build host against
clean commit `8859c867ef1a34365a63dbebbaa591cb74ef9bc9`. It produced identical
unpackaged and distributable executables:

```text
build/qemu-system-i386w.exe  44,211,197 bytes
dist/xemu.exe                44,211,197 bytes
SHA-256                      e963e6c13160114634f711953ed69565e626708449f852b0bdb02beaa0d661c8
build.log SHA-256            f94ee07f448a45ef49b1544b6ff735bf3e65a8a1cbc59494c67348fc328abed0
```

The effective log reports QEMU 10.2.0, GCC/G++ 16.1.0, and GNU ld
2.46.0.20260210 for `x86_64-w64-mingw32.static`. On the dedicated Windows test
host, that exact executable passed all six focused Vulkan report-query cases
and the complete 147-record Vulkan perf-lab XISO catalog. The XISO SHA-256 was
`08551d0c0b7bc5efb20a7b36d6d4f0e24ab666b4cee25930232858ccd9872a3e` and
the matching catalog SHA-256 was
`027065948624d6aafdbe557bed8123eb6dcaa83353cf242c71d030a109b64578`.
Because this branch lacks the lab's live-marker interface, the runner's
explicit compatibility waiver was used: the result is broad correctness/hash
evidence, not PR-grade timing evidence.

## Reviewed research branches

These branches remain open and are recorded here for traceability. They were
not merged wholesale because they are measurements, experiments, or contain
explicitly reverted experiments rather than an additional validated production
change.

| Branch | Result |
| --- | --- |
| `research/eng-2026-523-always-stage-vertex-layout-safe-ue4c43cdc` | Evaluated staging vertex RAM without layout changes; the validated production staging work is represented by the vertex-upload branch above. |
| `research/eng-2026-523-sparse-uniform-layout-safe-u6299d05f` | Investigated safely skipping clean uniform rows; retained as research pending an independently validated landing. |
| `research/eng-2026-523-tcg-tb-lookup-attribution-u065f47f7` | Collected indirect TB-lookup evidence that informed the later TCG fast paths. |
| `research/eng-2026-523-vk-stalled-gpu-timestamps-uf1f85872` | Added diagnostic Vulkan timing experiments and records several reverted candidates; it is intentionally not used as a production rollup branch. |
| `research/eng-2026-523-vk-tiny-draw-reuse-attribution-u42bc13ac` | Measured repeated inline-index and push-constant payloads in Morrowind. The push-constant skip was rejected. Exact consecutive inline-index reuse is retained as a bounded resource win after removing about 283 copies / 114 KiB per frame, but its balanced same-binary timing result was neutral. |

## Display controls

`Reduce host CPU usage when VSync is off`
(`display.window.reduce_host_cpu_usage`) is known-bugged as of 2026-09-03. It
must remain disabled in performance and correctness runs until the defect is
identified and fixed. Prior measurements showing lower host CPU use are
diagnostic history, not evidence that the option is safe to use or recommend.
Preserve the code and evidence for a focused investigation, but do not combine
or production-promote the behavior in its current state.

The setting remains disabled by default. The NVIDIA power preference is
automatic on supported Windows NVIDIA systems, and Vulkan telemetry remains
opt-in through `XEMU_VK_PERF_LOG`.
The combined Vulkan telemetry record uses schema version 5, which includes
both CPU-region and native-BC upload counters.

## Texture-only Vulkan pipeline lookup fast path

This candidate removes `texture_bindings_changed` as an independent reason to
rebuild and look up a Vulkan `PipelineKey`. Texture image and sampler identity
is descriptor-set state and is not present in `PipelineKey`. Texture registers
that can change generated shaders are still checked by
`pgraph_glsl_check_shader_state_dirty()` and represented by `ShaderState`;
`shader_bindings_changed` therefore retains the real pipeline dependency.
Texture descriptor updates and shader-uniform updates are not skipped.

Opt-in attribution on the Morrowind snapshot measured approximately 788 Vulkan
draws per guest frame. The candidate converted about 630 pipeline lookups per
frame into fast reuse, reducing lookups by 79.9% and measured pipeline-prepare
CPU time from 1.800 to 1.503 ms/frame. A short same-binary fixed-work S3TC
B-C-C-B test improved average work time by 1.079% and median work time by
0.977%; both candidate cells beat both baseline cells. This is a confirmed
small fixed-work saving, not a claim that Vulkan microstutter is solved.

The same-binary Morrowind B-C-C-B comparison was neutral within host variance:
candidate FPS was 0.220% lower and average frame time was 0.232% higher. The
50-ms tail remained present in that comparison. At implementation commit
`adebbf348b45b5344ae9b87bc68e83d4e1a83511`, the release-equivalent GCC
16.1.0 full-LTO build passed the current 147/147 Vulkan perf-lab catalog with
zero validation VUIDs. PGR2 and Morrowind saved-state captures also completed
with matching executable/PDB/source ownership, zero ETW loss, no focus or
responsiveness failures, and visually correct frames.

The validation media identities are:

```text
guest source    09f74db4822dbc3d34c3315d9a5cf5341f00416a
XISO SHA-256    08551d0c0b7bc5efb20a7b36d6d4f0e24ab666b4cee25930232858ccd9872a3e
catalog SHA-256 027065948624d6aafdbe557bed8123eb6dcaa83353cf242c71d030a109b64578
catalog records 147 (142 executable leaves and 5 groups)
```

## NV2A PTIMER overdue catch-up

Fresh PGR2 exposed a renderer-independent regression that snapshot restore
masked. At release-equivalent commit
`1a5e2aa87f497b89f787023f3a2c098b93697c37`, fresh Vulkan averaged 6.722
FPS and fresh OpenGL averaged 8.203 FPS while a Vulkan snapshot restored at
30.000 FPS. Scheduler evidence attributed the fresh-load loss to repeated
immediate PTIMER callbacks around the big QEMU lock.

Commit `35ebe08da7619a935aaa3a3df75dfb69e0ec949c` retains the single pending
interrupt bit but calculates the next matching future deadline from current
PTIMER time rather than advancing only one missed epoch. Its deterministic
regression case fails against the old implementation and all six PTIMER unit
cases pass with the fix. The clean full-LTO build recovered fresh PGR2 to
30.000 FPS on Vulkan and 30.002 FPS on OpenGL. Vulkan p95/p99 were
33.537/34.329 ms, and its TCG BQL sample share fell from 58.0% to 0.36%.

The fix was revalidated in both full-LTO release and `-O0` non-LTO debug
builds. The release build passed the current 147-record XISO with functional
hash validation and active Vulkan validation, and the heavier Morrowind
snapshot rendered correctly at 28.787 FPS with a 50.005 ms p95. Debug timings
are diagnostic only.

## Consecutive inline-index reuse

`XEMU_VK_REUSE_IDENTICAL_INDEX_PAYLOADS=1` enables a bounded research
experiment at commit `c51f5271e26ace1f11dc2bb65507b8bcc107e604`. It reuses
an index staging offset only when the immediately preceding payload has the
same size and exact bytes in the same command buffer. The remembered offset is
invalidated at command-buffer begin, so no cross-command-buffer or in-flight
memory is reused.

A full-LTO same-binary A-B-B-A run on Morrowind snapshot
`vm-20260903021051` eliminated 282.84 copies and 113,738 index bytes per guest
frame. Total staged bytes fell 1.176%. Timing was neutral: enabled versus
disabled was -0.035% FPS, -0.288% mean frame time, -0.005% p95, and +0.424%
p99. All four p95 values remained approximately 50 ms. Retain this as a small
resource/copy reduction, not as a cadence fix.

The release candidate also restored the new PGR2 race at 29.940 FPS and the
debug build reached live Morrowind plus a PGR2 FreshBoot race. The exact
current perf-lab gate passed 147/147 with functional hashes, active Vulkan
validation, zero VUIDs, and the enabled environment recorded in its summary.
Before production review, extract the index-reuse path from this research
branch so the rejected push-constant skip is not coupled to the accepted
resource decision.

PGR2 snapshot restore remains a separate reliability lead: release restore
hit `dsp_cpu.c:893:read_memory_p` on one of two launches, and debug restore hit
the same assertion on both attempts before draw measurement began. Debug
FreshBoot then reached a live race using a 10-second BIOS allowance followed
by `A-3,A-10,A-2,A-2,F-2,A-2,A-2,A-2,A-2,A-2,A-7`. Do not merge snapshot
and FreshBoot results into one timing cohort.

Exact build identities for this experiment are:

```text
source commit                 c51f5271e26ace1f11dc2bb65507b8bcc107e604
release post-cv2pdb xemu.exe e6863bcdc827a069eb859fec329d7b3efaa0c444612fe1b79ad32af3a87e0311
release PDB                   dd256c54021332747383cd9ce15c90980afcfe59728cfd002e5af61248d80a60
release DWARF executable      15e8118e6a11313ea4d56cc8aa587594d41d1cbb890395d554ff0b0610edb86e
debug post-cv2pdb xemu.exe    6d554667748f9f75bb1e5bd06e27c5d3e4a91e17b42163594d5cc9299d745758
debug PDB                     9882c3face26ce9f7a6f57368f614181db6e18db743607ec72954de2b47f1e9e
debug DWARF executable        ad6e356b2aede0281bd644ff064db0b90bc2da88a7b0637acc2228acb287b886
```

The exact release and debug commands, pinned container digest, compiler,
linker, Meson, Ninja, `cv2pdb`, map generation, and artifact requirements are
the common reproducibility contract above. This experiment adds no private
toolchain or branch-specific compiler flag.

## Staging audit

The `xemu-pr-staging` production candidates were compared against this
rollup. The transient-buffer growth, changed-uniform-row, versioned-vertex,
dynamic-blend, and texture-dirty branches already have later validated
successors here. ENG-2026-336 was the remaining distinct production change;
its seven focused unit tests passed on the Linux dev box under Wine before it
was reconciled into this branch. Staging source branches remain open.

## Validation focus

Validate this rollup with the same snapshot and emulator configuration used by
the baseline. Compare guest-event median, p95, p99, and maximum duration;
Vulkan finish/wait time; ordered texture-upload and vertex-staging counters;
native-versus-decoded BC upload counters; and TCG CPU time. Keep the source
branches available for A/B comparison rather than closing or deleting them.
