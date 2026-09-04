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

## This branch: guest-visible Vulkan report boundary

`fix/eng-2026-523-vk-report-visible-boundary-ud54b59a7` is a correctness-only
child of the report-DMA branch at `16a555238a1f7d3adffa9cbd4c369550c1d5e934`.
It adds no texture-pipeline or batched-submit performance work.  Vulkan now
submits and resolves the query prefix when the guest issues `GET_REPORT`, so a
later clear cannot be appended ahead of publication of the earlier report.
The finish remains synchronous and the queued report continues to own the DMA
target captured by its parent branch.

This is the second step in the cumulative correctness foundation:

```text
16a55523  report DMA ownership
    + this branch: guest-visible report boundary
        + later independently validated correctness repairs
            + one isolated performance candidate per child branch
```

Performance branches must all start from the same final correctness-foundation
SHA.  A performance branch carries only its own candidate delta; it must not
inherit another performance candidate.  The combined `Full-Speed` branch is
tested only after the isolated children.

The initial controlled A/B used the same batched-submit candidate and changed
only this boundary behavior.  The unfixed executable failed
`report_query.clear_boundary` in 3/3 Release runs.  The repaired executable
passed the complete 147-record XISO catalog in 3/3 Release and 3/3 Debug runs.
Those runs prove the change on that stressed candidate; this clean branch still
requires its own exact-head Release and Debug gate before publication.

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
| Display setting A/B | One production binary with `display.window.reduce_host_cpu_usage` toggled at runtime | Isolates the setting without compiler or source drift |

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

## Display controls

The rollup has one display-side CPU-saving preference:

`Reduce host CPU usage when VSync is off` (`display.window.reduce_host_cpu_usage`)
is disabled by default. When enabled while VSync is off, it both uses efficient
host timer waits and yields briefly between uncapped presentation frames. The
same setting is declared once in `config_spec.yml` and rendered once in the
Display menu; no competing toggle was introduced. The NVIDIA power preference
is automatic on supported Windows NVIDIA systems, and Vulkan telemetry remains
opt-in through `XEMU_VK_PERF_LOG`, so neither adds a second UI control.
The combined Vulkan telemetry record uses schema version 5, which includes
both CPU-region and native-BC upload counters.

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
