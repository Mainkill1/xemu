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

For an assertion-enabled diagnostic build, use the same clean source commit,
container digest, target, environment, and mounts, but replace the `build.sh`
invocation above with:

```bash
./build.sh -j"$(nproc)" -p win64-cross \
  --debug -Db_lto=false -Dx86_version=3
```

That command produces the lab's `-O0`, `--enable-debug`, log-trace build. It is
required for debug correctness coverage, but its timings are not compared with
the full-LTO performance build.

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
The lab's `cv2pdb64.exe` SHA-256 is
`93b9033f24a9d671544c885bea29f825920199fb929cff9f1ae877b141f49184`.

Generate the sorted GNU symbol map from the original DWARF executable before
running `cv2pdb`. The `nm` binary comes from the same pinned MXE container:

```bash
x86_64-w64-mingw32.static-nm -n \
  build/qemu-system-i386w.exe > xemu.map
```

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
| `research/eng-2026-523-vk-tiny-draw-reuse-attribution-u42bc13ac` | Measured repeated inline-index and push-constant payloads plus descriptor-pool exhaustion in Morrowind. The push-constant skip was rejected. Exact consecutive inline-index reuse and a bounded opt-in 2,048-set graphics pool are retained as small resource/submission wins; neither fixes the approximately 50-ms cadence tail. |

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
The combined Vulkan telemetry record uses schema version 10. It includes
CPU-region and native-BC upload counters, disaggregated buffer-space finish
owners, descriptor-set capacity/high-water fields, and vertex dirty-check,
page, hit, and recent-range-reuse counters. Pipeline preparation is split into
texture binding, shader binding, and pipeline state/key/cache lookup regions.

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

## Bounded graphics descriptor-set expansion

Telemetry commit `acedd333e7db82f317ab45bc632031695ca954b1`
split the old `NEED_BUFFER_SPACE` finish reason into nine call-site owners
without changing synchronization. On the heavier Morrowind snapshot
`vm-20260903021051`, `need_buffer_space_uniform_or_descriptor` was the only
active split owner: 1.002 calls/frame and 5.095 ms/frame, or about 76% of all
measured Vulkan wait time. The other eight split sites were zero.

Commit `1c35d2e7b6a5c2e1b251c31e42c924858fb6b961` adds the opt-in
`XEMU_VK_EXPAND_DESCRIPTOR_SETS=1`. Disabled preserves the old 1,024-set pool,
allocation, and reuse boundary. Enabled allocates a bounded 2,048-set graphics
pool; it does not allow an in-flight descriptor set to be rewritten. Both
capacities fit in a fixed 2,048-handle array. Telemetry records the
selected capacity and per-frame descriptor high-water.

A same-binary full-LTO A-B-B-A run proved the resource mechanism. Both
1,024-set cells reached a high-water of exactly 1,024 and paid 4.973 ms/frame
to the exhaustion finish. Both 2,048-set cells eliminated that finish and
reached 1,585--1,586 sets. Vulkan submissions fell from 5.659 to 4.757/frame
(-15.94%), and total measured wait fell 0.223 ms/frame (-3.23%). The removed
GPU completion mostly shifted to later required `stalled` and `surface_down`
boundaries, so end-to-end improvement was small: +0.303% FPS and -0.126
ms/frame (-0.359%) by role mean. P95 improved 0.009 ms while p99 regressed
0.040 ms; the approximately 50-ms tail remained. NVIDIA's MiB-granularity
memory counter was identical across all four cells (762--767 MiB, mean 763.65
MiB), so no VRAM increase was observable at that resolution.

Retain this as a bounded small submission/wait win, not as a cadence fix. The
release and debug builds both passed the current 147-record Vulkan perf-lab
XISO with functional hashes and zero VUIDs. Debug Morrowind reached descriptor
high-water 1,585 with zero exhaustion finishes. Release PGR2 snapshot
`vm-20260903022956` completed at 30.000 FPS, and debug PGR2 FreshBoot reached a
live race using the separately recorded 10-second BIOS allowance and exact
input sequence.

Exact build identities are:

```text
source commit                 1c35d2e7b6a5c2e1b251c31e42c924858fb6b961
release post-cv2pdb xemu.exe 485209f1bd624d6ac25b086521816f86a789839499b280ab2cfa3feb4c2a851c
release PDB                   d3ad08dbfa45ca723c18533aa903f1353a1edaf60007fe7ab1f4e652b595e0d6
release DWARF executable      d157d460299683b8843a7ec819ca2eb99426abe276a6e76b526a1149dbcd424a
debug post-cv2pdb xemu.exe    d4a2f4e106a3c784cf1abf1534962d2cf0a490f27207d4bf0d2fc614ab23b789
debug PDB                     f6cf2a60df942602c1ac2130ade5d268c4b40546baf9e06541f7586c4ac4974d
debug DWARF executable        46766cfbfca33975699aa614064c94ccd08257dda5dab5959a9e7ccfd0bddaba
```

## TLB dirty host-page prefilter experiment

The heavier Morrowind snapshot averages about 1,116 vertex dirty checks and
337 ordered vertex staging copies per guest frame. Exact PFIFO ETW attributes
about 18.2% of resolved PFIFO CPU samples, approximately 1.8 ms/frame, to
`tlb_reset_dirty_range_all()`. Schema-9 telemetry also shows that only about
9.8 dirty hit ranges/frame repeat within the recent eight-range window, so an
exact renderer-range cache is not a useful attack vector.

`XEMU_TCG_TLB_DIRTY_HOST_PAGE_FILTER=1` enables a narrower same-binary
experiment. Writable RAM translations cache their already-computed host page
in the full TLB entry. Dirty reset still visits every active MMU mode and
retains the original flag and range test for every possible match, but rejects
nonmatching host pages before reading and updating the hot fast-entry state.
Aliases remain independent entries and therefore remain covered. Unset or `0`
preserves the prior scan behavior.

A full-LTO same-binary A-B-B-A run on Morrowind snapshot
`vm-20260903021051` measured +0.431% FPS and -0.084 ms/frame (-0.236%) by role
mean. P95 and the approximately 50-ms cadence were unchanged. The more direct
CPU measurement was stronger: `draw_flush` fell from about 12,046 to 10,983
us/frame (-8.82%), and its normalized cost fell from about 10.663 to 9.722
us/draw. Exact PFIFO stacks independently showed the complete
`tlb_reset_dirty_range_all` chain falling from 2,487 to 1,908 weighted samples
(-23.28%) even though candidate dirty hits/frame were slightly higher. Treat
this as a causally confirmed small CPU-path win, not a cadence fix.

Release and debug builds both passed the current 147-record Vulkan perf-lab
XISO with functional hashes, active validation, and zero VUIDs. Because the
new PGR2 snapshot reproduced the existing DSP restore assertion, retail
coverage used an independent FreshBoot cohort. Release reached a visually
verified live Hong Kong race at 30.000 FPS (33.333-ms mean, 33.456-ms p95);
debug also reached the live race after its deliberately longer 60-second
post-input warmup. Both runs recorded a 10-second BIOS allowance followed by
`A-3,A-10,A-2,A-2,F-2,A-2,A-2,A-2,A-2,A-2,A-7`, exact executable/PDB/source
ownership, zero focus loss, and zero ETW loss. Snapshot and FreshBoot results
remain separate cohorts.

Exact build identities are:

```text
source commit                 93d099ac92fd706743c869d1c2cdc421aa036f38
release post-cv2pdb xemu.exe d98148685abbc21d6c5d66f307e20eb17bbb593a43c2cf7472fac972b38737f4
release PDB                   f5ff29e41303e7702ae931cb330c00e8cbfda9629bd8399e003d8090ccd69752
release DWARF executable      d74a1cf1d15fdebd62bea91eaa9eb57992890024cc872a0a1298a6809bb3f984
release map                   108db08877e02ccf8559ef43debf06f87dc536682ab491475a9225f7683bcdae
debug post-cv2pdb xemu.exe   ad984c9158f1bfb2bb093e3333ee2b403e12218b5a26b3a3bf964dcc2bcbb6a6
debug PDB                     64ae29effb524bfdd65e53f48d8a94b2b8715d12aef837d310a62f64b73b9d11
debug DWARF executable        f92af1dce9e401aa800a2655cc8cdf261d6ac9f661bafc3efc4312aeca1e1337
debug map                     4f24bedede729710bbe35eb625c0dd2a3e381a6eab8dc0c9222ed733f7bb3e39
```

The build manifests retain the exact release/debug commands, pinned toolchain
digest, compiler/linker versions, map generation, and all artifact hashes.

## Pipeline-preparation attribution

Telemetry schema 10 at commit
`277a91da06fb594efec6e08ea9a21164af301a6a` divides the existing pipeline
preparation CPU region into texture binding, shader binding, and pipeline-state
lookup without changing renderer behavior. A clean full-LTO capture on
Morrowind snapshot `vm-20260903021051` measured 571 guest frames at 28.368 FPS,
35.388-ms mean, 50.015-ms p95, and 50.170-ms p99.

Pipeline preparation cost 2.568 ms/frame. `pgraph_vk_bind_shaders`, including
shader-state and uniform preparation, owned 1.323 ms/frame (51.5%); texture
binding owned 0.879 ms/frame (34.2%); and pipeline dirty checking, key/hash
construction, and cache lookup owned only 0.245 ms/frame (9.6%). Pipeline
hashing is therefore not the next primary target. The next attribution step is
inside shader binding: separate shader-state checks and binding changes from
uniform need checks and actual VSH/PSH updates before changing uniform dirty
tracking.

The 50-ms frames also carried more work than the 33-ms bucket: dirty hits rose
from 341.03 to 353.78/frame, draw-flush CPU from 10.882 to 11.769 ms/frame,
pipeline preparation from 2.560 to 2.689 ms/frame, and surface-download waits
from 2.600 to 3.005 ms/frame. `stalled` waits decreased from 2.820 to 2.679
ms/frame, so that wait is not the sole cadence-transition owner. Small constant
CPU wins still matter because a frame near a 16.7-ms presentation boundary can
otherwise fall into the next cadence step.

Exact schema-10 release artifact hashes are:

```text
source commit                 277a91da06fb594efec6e08ea9a21164af301a6a
release post-cv2pdb xemu.exe 5e6c3dd51e397222d89270602c2119f95abbb008305a54e58de24ddcc2e97e79
release PDB                   721ed01cb3d2e24ad082a76f9fbecfcf3cda6ad39300f514e55987c7fa0891f4
release DWARF executable      94c3a23f17ee8e6ec49e242f7d8ad3385c5b16e7561f0b0666a3c347e25eca2b
release map                   e1ea02ba4703daeac8e8de1cb9fd35a24710102dc1d739584a9b289e5f21cffb
```

Telemetry schema 11 added the first shader-binding diagnostic layer. It times
`shader_state_prepare`, `shader_uniform_needs`, and
`shader_uniform_update` separately, and records bind calls, state checks,
dirty results, binding changes, VSH/PSH update requests, and no-update exits
per guest frame. These counters remain inactive unless `XEMU_VK_PERF_LOG` is
set. Schema 12 additionally attributes each VSH/PSH update request to its
source, layout, texture-binding, effective-input, inline-value, dirty-row, or
forced-update cause, and records whether the copied VSH/PSH values actually
changed. Diagnostic timing overhead is not treated as a production result.

Schema-11 measured actual uniform preparation at 0.925 ms/frame and its
update-needs scan at only 0.098 ms/frame. Schema-12 then showed the actionable
false dependency: Morrowind reported texture bindings changed on all
1,133.83 draws/frame and therefore requested a PSH uniform update on every
draw, while the resulting PSH uniform bytes changed only 0.01085 times/frame.
Texture descriptor writes remain independently guarded by
`texture_bindings_changed` and must not be skipped.

`XEMU_VK_SKIP_EQUIVALENT_TEXTURE_SCALE_UPDATES=1` enables the bounded
schema-13 experiment. When a texture binding changes, it derives the four
effective PSH `texScale` values and compares their exact float bits with the
last values sent through PSH uniform preparation. Equivalent values no longer
force that uniform update; changed values, first use, shader-layout changes,
PSH source changes, and forced buffer refreshes retain their existing paths.
Texture image/sampler descriptor updates are not changed. Unset or `0`
preserves the previous coarse dependency for same-binary A/B testing.

A clean full-LTO same-binary A-B-B-A run confirmed the direct CPU effect.
PSH uniform requests fell from 1,132.16 to 594.05/frame (-47.53%), uniform
update CPU fell from 937.44 to 750.56 us/frame (-19.93%), and total shader
binding fell from 1,462.24 to 1,273.24 us/frame (-12.93%) by role mean.
Descriptor-update CPU remained approximately flat. The end-to-end role means
were directionally positive: FPS rose 1.63% and guest frame time fell 0.490 ms
(-1.36%). Treat that timing as supporting evidence because host/run variance
is larger than the causally measured 0.187-ms uniform saving.

This does not fix the Vulkan tail. P95 was 50.0105 ms disabled and 50.0095 ms
enabled; p99 was 50.0345 ms in both role means. Retain the experiment as a
bounded opt-in CPU-path win while the separate approximately 50-ms cadence
investigation continues.

Release and assertion-enabled debug builds each passed the current 147/147
perf-lab XISO, functional hash validation, active Vulkan validation, and zero
VUIDs with all retained opt-ins recorded. Both builds rendered the heavier
Morrowind snapshot correctly. PGR2 was tested as a separate FreshBoot cohort:
exactly 10 seconds of BIOS allowance, then
`A-3,A-10,A-2,A-2,F-2,A-2,A-2,A-2,A-2,A-2,A-7`. Release reached a live race
at 30.000 FPS; debug also reached a visually correct live race after its
separate 60-second post-input warmup. All retail captures had exact
executable/PDB/source ownership, zero focus loss, and zero ETW loss.

Exact build identities are:

```text
source commit                 069e92211c64d56c9309a7d26241c442018e6fdb
release post-cv2pdb xemu.exe a74ff6e318d1cec6299714c9c4992317dcff825e9f1304b985ba184a799dcf10
release PDB                   63747f22edef6edbf99aa9ed07cf788cfd10e1f7cbdf3b39cad1c05989750d4b
release DWARF executable      e93fb85496ef5aee6c21903f8796af83300aba3d6928ba4d9ec37365c3c93a22
release map                   8556ef76d825727d456486355d24bb86a7e1f3700a68bb32349fd39c7bd68e3a
debug post-cv2pdb xemu.exe   3e43c3d3ba6fd38cedaa67ba87a3d4f6fcab71a22808793fa6069c3caa3246b5
debug PDB                     cb4f5be24056009bffd313baa0c2d3d70e73bb6baa839432d04c0d2b4e83f586
debug DWARF executable        9f320e2be7bb4557223e1f0077ddcdf476572885fbadd00fffce462efce7419c
debug map                     2ff9b92ae03f7871a870beffa554de37f20f9a6861f90087c9842135747694a2
```

The release/debug build manifests beside those artifacts record the exact
commands, checkout, pinned public toolchain digest, compiler/linker/Meson/Ninja
versions, `cv2pdb` source and hash, map command, and unit-test hashes. No
private compiler or unrecorded branch-specific build step is required.

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
