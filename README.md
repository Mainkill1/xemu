# Full Speed

`Full-Speed` is the consolidated research-integration branch for the xemu
changes reviewed after the original Full-Speed baseline (`6234f3606f`). It is
not a production or upstream-ready branch. Forgejo issues #37 and #38 found
correctness defects and validation gaps in both the rollup and its source
branches. Source branches remain available as experiment history; production
candidates must be extracted onto a clean target with their own tests.

This corrective branch is based on `Full-Speed` commit `111deac13f` and fixes
the immediately confirmed rollup defects: complete PTIMER reconciliation and
defined zero-ratio handling, all-CPU callback-retirement ordering, removal of
the unsafe host-load timer control, and removal of the persistent NVIDIA
maximum-power policy. The remaining `Hold` entries below still require the
listed differential or rendered-output gates.

## Included branches

| Branch | Status in Full-Speed | Change |
| --- | --- | --- |
| `feature/eng-2026-520-perf-etw-event-telemetry` | Diagnostic only | Adds opt-in NV2A retail-capture telemetry. Its event naming, disabled-path overhead, sampling, and placeholder fields must be corrected before it is trusted as proof. |
| `feature/eng-2026-525-vk-wait-telemetry-u524a4d5b` | Superseded by v2 | Introduced per-reason Vulkan submit/wait attribution. Its functional changes are retained through the v2 branch. |
| `feature/eng-2026-525-vk-wait-telemetry-v2` | Included | Reduces telemetry overhead by timing the first occurrences and periodic hot occurrences of each Vulkan wait reason. |
| `feature/eng-2026-523-vk-vertex-upload-staging` | Hold | Stages ordered vertex-RAM updates and removes measured waits, but capacity pressure, overlap, reset, and rendered-output validation remain required. |
| `feature/eng-2026-523-vk-cpu-hotpath-attribution` | Included | Attributes Vulkan draw-preparation CPU work in the opt-in telemetry output. |
| `feature/eng-2026-523-win-qpc-clock-fastpath` | Included | Avoids an unnecessary 128-bit divide in exact Windows QPC scaling. |
| `feature/eng-2026-523-win-nvidia-prefer-max-performance-u90829d4e` | Removed | Persistently changed driver power policy without a separate opt-in or rollback and could prevent the older profile settings from being saved. |
| `feature/eng-2026-523-tcg-active-mmu-dirty-reset-ud9e1e15a` | Hold | Skips empty MMU modes while resetting code-dirty state; requires isolated randomized equivalence coverage across active/victim modes and resize/flush transitions. |
| `feature/eng-2026-523-tcg-x86-static-state-tb-lookup-u3121717e` | Hold | Contains several independent TCG and memory fast paths. Each source change must be isolated and differential-tested before promotion. The cached callback lifetime defect is fixed on this corrective branch. |
| `feature/eng-2026-523-vk-native-bc-on-vertex-staging-u8a09a4ea` | Hold | Native BC layout arithmetic is covered, but native-versus-decoded Vulkan upload/sample output still needs comparison. |
| `xemu-pr-staging: feature/eng-2026-336-stage-local-vulkan-uniforms-v3-uec89c153` | Hold | Stage-local uniform updates need table-driven dependency coverage plus UBO-byte and rendered-output comparison against the always-update path. |
| `fix/eng-2026-523-vk-report-dma-ownership` | Included | Captures the report DMA object when a GL, Vulkan, or null-renderer query is queued, preventing a later context switch from writing the completed report through the wrong DMA mapping. |
| `feature/eng-2026-523-vk-texture-pipeline-fastpath-uaad84ed1` | Included | Keeps texture image/sampler changes on the descriptor path instead of forcing a Vulkan pipeline lookup; shader-affecting texture state still invalidates through `ShaderState`. The recorded Morrowind capture reduced pipeline lookups by 79.9%, and the fixed-work A/B comparison improved average throughput by 1.079%. |
| `fix/eng-2026-523-nv2a-ptimer-overdue-catchup-u76925787` | Corrected here | Coalesces missed epochs. This corrective branch uses one transition for callback, post-load, interrupt-read, and interrupt-enable paths, asserts exact future epochs, and treats a zero ratio as a stopped clock without changing the raw registers. |

## One-variable review and A/B branches

Do not use this combined corrective branch to attribute a performance change.
Use the isolated branch that owns the relevant behavior and compare it with
the exact parent recorded below. Small improvements remain valid findings, but
they must keep their own confidence level and correctness evidence.

| Concern | Isolated branch/head | Exact control | Purpose |
| --- | --- | --- | --- |
| PTIMER reconciliation and zero ratios | `fix/eng-2026-523-ptimer-reconciliation-zero-ratio-u3f16e05d` (`b332c16d6a`) | `Full-Speed` (`111deac13f`) | Correctness isolation; measure timing only as a separately proven lead. |
| Cached callback retirement | `fix/eng-2026-523-callback-retirement-order-u16a212b2` (`1f0dbd4d5e`) | `Full-Speed` (`111deac13f`) | Prove all CPUs drop cached references before free. |
| Unsafe host-load option removal | `fix/eng-2026-523-remove-unsafe-host-load-ube3040c6` (`4140a916fb`) | `Full-Speed` (`111deac13f`), legacy option disabled | Establish the safe baseline without global timer changes. |
| Host-load feature replacement | `research/eng-2026-523-host-load-deadline-redesign-ufaef5c6e` (`676c7f3c84`) | Safe-removal branch (`4140a916fb`) | Preserve the valuable feature as a presentation-deadline-only opt-in design. |
| NVIDIA persistent PSTATE policy | `fix/eng-2026-523-remove-nvidia-power-policy-u83f2a080` (`ede650cf47`) | `Full-Speed` (`111deac13f`) | Keep driver state outside implicit emulator startup behavior. |
| Tiny-draw empty-TLB metadata access | `research/eng-2026-523-vk-tiny-draw-reuse-attribution-u42bc13ac` (`091dc945fd`; behavior `45fbf9e10c`) | Its behavior commit's immediate parent | Safety fix inside research history; extract again before performance promotion. |
| Batched-submit dependencies | `fix/eng-2026-523-vk-batched-submit-safety-u70dcad5c` (`2991e35eea`; behavior `84975084de`) | Rejected batched branch at `d13650db` | Synchronization repair only; neutral/slower result remains rejected. |

Telemetry semantics, each TCG/MMU optimization, vertex staging, texture
pipeline classification, native BC output, uniform invalidation, and
equivalent texture-scale suppression remain separate extraction tasks. They
must not be grouped into a new performance branch merely because they share
this investigation number. Each extracted branch must state A/B/C source
commits, release/debug builds, perf-lab XISO coverage, the relevant retail
fresh-boot or snapshot workload, and its hypothesis-specific metric.

### Corrective-branch validation to date

The exact behavior head `2ed201fe4fe93e833f6c6b105098d9889be4e765`
builds successfully in both the pinned release/LTO configuration and the
assertion-enabled debug configuration documented below. In each build,
`tests/unit/test-xbox-nv2a-ptimer.exe --tap -k` passed all 9 tests under Wine,
including callback, post-load, interrupt-read, interrupt-enable, exact
multi-epoch, and zero-ratio cases. This proves compilation and focused PTIMER
behavior; it does not replace the pending perf-lab XISO, multi-vCPU callback
stress, Vulkan synchronization validation, or fresh-boot retail gates.

## Reproducing the Windows builds

No private compiler, patched SDK, or hidden branch-specific flag is required.
The authoritative upstream job is [`.github/workflows/build-windows.yml`](.github/workflows/build-windows.yml).
The lab reproduced its x86-64 release path on a Debian 13 x86-64 Docker host
(Docker 29.7.2); those host versions are descriptive, while the immutable
container below defines the compiler environment.

### Source identity

Use a clean checkout of the exact 40-character commit under test:

```bash
git clone --recurse-submodules <published-xemu-repository> xemu-full-speed
cd xemu-full-speed
git fetch --all --tags
git checkout <full-40-character-commit>
git submodule update --init --recursive
git status --porcelain=v1
git rev-parse HEAD
```

The status command must print nothing before the build. A binary produced by
copying changed files into another checkout is diagnostic only and is not
release-eligible.

### Pinned upstream toolchain

| Component | Exact value |
| --- | --- |
| Container tag | `ghcr.io/xemu-project/xemu-win64-toolchain-gcc:sha-2881edd` |
| Container digest | `sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2` |
| Base image | Ubuntu 24.04 |
| MXE source | `https://github.com/mxe/mxe.git` at `d9441093aa48e376aa6e49bcc7118ccc2b683a1e` |
| Target | `x86_64-w64-mingw32.static` |
| Compiler | GCC 16.1.0 |
| Linker | GNU binutils 2.46.0.20260210 |
| Meson selected by `build.sh` | 1.9.0 |
| Ninja | 1.13.2 |
| Python | 3.12.3 |

Pull by digest so a mutable tag cannot change the build environment:

```bash
docker pull \
  ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2
```

### Release/LTO build used by the lab

Run from the clean source root. The cache affects build time only. `curl` is
installed because the pinned image does not include it and xemu's DSP fallback
downloads a versioned input while configuring.

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
    ./build.sh -j32 -p win64-cross \
      --extra-cflags="-flto-incremental=/xemu-cache/lto -flto-partition=cache" \
      -Db_lto=true -Dx86_version=3'
```

This is the x86-64 release-equivalent path used for shared performance
binaries. The official workflow chooses the runner's concurrency instead of
the lab's `-j32`; that changes build duration, not generated semantics.

### Debug/assertion build used by the lab

Use a separate clean checkout or build directory with the same container:

```bash
./build.sh -j32 -p win64-cross --debug -Db_lto=false -Dx86_version=3
```

The official Windows workflow uses `--debug` for its debug matrix entry. The
lab also states `-Db_lto=false` and `-Dx86_version=3` explicitly so the
assertion build's configuration is unambiguous. Debug timings are not compared
against release/LTO timings.

`build/qemu-system-i386w.exe` is the unbundled executable. `build.sh` produces
`dist/xemu.exe` and `dist/LICENSE.txt`. Preserve `build.log`, the clean-source
result, the full command, and the exact source commit.

### Windows symbols and packaging

The upstream job uses public `cv2pdb` 0.52 from
`https://github.com/rainers/cv2pdb/releases/download/v0.52/cv2pdb-0.52.zip`.
The lab's `cv2pdb64.exe` SHA-256 is
`93b9033f24a9d671544c885bea29f825920199fb929cff9f1ae877b141f49184`.
Before conversion, retain the DWARF executable and create the sorted symbol
map with the matching container tool:

```bash
x86_64-w64-mingw32.static-nm -n \
  build/qemu-system-i386w.exe > xemu.map
```

Then run on Windows:

```powershell
Copy-Item xemu.exe xemu-dwarf.exe
cv2pdb64.exe xemu.exe
Get-FileHash -Algorithm SHA256 xemu.exe,xemu-dwarf.exe,xemu.pdb,xemu.map
```

Distribute the executable together with `LICENSE.txt`, matching PDB, original
DWARF executable, map, build log, source commit, build identity, and SHA-256
manifest. Never symbolize an address with artifacts from another executable.

### Required validation identity

Each result bundle must identify the emulator commit and hashes, renderer,
VSync state, output resolution, GPU/driver, Windows build, snapshot or fresh
boot path, and perf-lab XISO/catalog hashes. The current broad correctness gate
contains 147 records. PGR2 fresh-boot automation uses a 10-second BIOS delay,
then `A-3,A-10,A-2,A-2,F-2,A-2,A-2,A-2,A-2,A-2,A-7`; snapshot and fresh-boot
runs are different test modes and must not be presented as interchangeable.
Behavior changes are tested in both release and debug builds. The removed
host-load option must remain unavailable/off in normal runs.

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
| `feature/eng-2026-523-vk-aux-submit-fence-u70f4e0ef` | Replaces a queue-wide idle wait with an auxiliary-command-buffer fence. It passed focused validation, but its recorded end-to-end A/B result was neutral to slightly slower; it remains a candidate, not a rollup change. |
| `feature/eng-2026-523-vk-batched-aux-main-submit-u48cab4ee` | Batches the auxiliary and draw command buffers into one ordered submit. Its measured A/B result was neutral/slower, so it remains research. |
| `research/eng-2026-523-vk-report-boundary-submit-u688e8e71` | Records report-boundary submission experiments and remains open pending a reproducible end-to-end improvement. |
| `research/eng-2026-523-vk-tiny-draw-reuse-attribution-u42bc13ac` | Contains further tiny-draw and cache-attribution experiments; it is deliberately not merged wholesale. |

## Removed host-policy experiments

The `Reduce host CPU usage when VSync is off` option is not available on this
corrective branch. Its implementation coupled a display preference to global
QEMU timer precision and added an unconditional 1 ms display-loop delay. The
observed host-load reduction remains a valuable lead tracked by issue #37, but
it must return only as an explicit toggle based on a real presentation/event
deadline and must not change guest timer semantics. Do not use older builds
with this option enabled for correctness or performance comparison.

The NVIDIA maximum-performance P-state addition is also removed. The existing
NVIDIA OpenGL profile setup remains unchanged; xemu does not silently add a
new persistent maximum-power policy on this branch.

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
For the new work, also run `test-xbox-nv2a-ptimer`, verify report-query writes
against the DMA object active at queue time, and compare Vulkan pipeline
lookups plus draw-preparation CPU time on a texture-heavy saved-state capture.
