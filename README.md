# xemu Full-Speed

`Full-Speed` is the cumulative xemu performance and correctness candidate. It
is an append-only upgrade of the existing Full-Speed branch at
`e2c77c38a5d403da10f75df329f356f754aeb1c2`; it was not rebuilt by squashing,
rebasing, or replacing that history.

The release executable is built from the frozen code commit
`ccdb31d508c2c929d839b05a1d2717b5b59868c4`. The later README commit changes
documentation only. The exact executable and package hashes are published in
the release manifest so another tester can prove that they have the same
binary.

This branch is a combined candidate, not a claim that every retained component
improves every game. Correctness repairs are accepted on correctness evidence;
performance changes still require the A/B protocol below.

## Reference baselines

Do not use an unnamed local executable as “baseline.” Record both the source
commit and executable SHA-256.

| ID | Purpose | Source commit | `xemu.exe` SHA-256 |
| --- | --- | --- | --- |
| Official U | Unmodified [xemu-project/xemu](https://github.com/xemu-project/xemu) reference | `fc24584ce88f0915ad7f04775bb7712c2e3f49ee` | `7da537938ea2ac09f894186ba793c9ae51dff37c95903b0002273b8d363818b7` |
| Stable S | Current 100%-passing correctness baseline, published as [eng523-pr126-texture-contract-v2](https://github.com/Mainkill1/xemu-full-speed/releases/tag/eng523-pr126-texture-contract-v2) | `bc60883c4ef05912c5b4b29051ba64341f576b15` | `9cfc03ebfa7bf3ba727c48dd90782a455381c0ae09341f76a7a08d82cc6f3606` |
| Full-Speed candidate | This combined code candidate | `ccdb31d508c2c929d839b05a1d2717b5b59868c4` | `43e037d9bafc464486cdbee59fac8468a10ac41e91852eaf851a22e677b07b13` |

After the combined candidate passes the complete release gate, its packaged
executable becomes the next Stable S. Until then, Stable S remains the
performance-patch control.

## What is included

The ongoing branch already contained the following reviewed production work.
The listed commits identify the functional branch lineage; later conflict or
correctness repairs in Full-Speed take precedence.

| Area | Functional source | State in Full-Speed |
| --- | --- | --- |
| Active-node LRU visitation | `a9c0cd70d089e29e5787de1dbcfe75bbd388bcaa` | Retained |
| Selective atomic-fence/BQL fast path with reentrancy repair | `418425b6ba6bb24a596740ea87bd4a2e87d13f1c` | Retained repaired form |
| Dynamic Vulkan blend state and transition fixes | `f0757df626f2a1fbfdd3dd217fdf8cc5f60feae4` | Retained |
| Native Vulkan BC1/BC2/BC3 upload with fallback | `d651faf545c5db86d0c33e3be021eedef34fa4eb` | Retained |
| Ordered texture staging | `82d125226d34218fb7d8b766809bb38cd3267439` | Retained and reconciled with texture fixes |
| Stage-local Vulkan uniforms | `b1133ae032d2a6dcabc5307bf1e5fa666a59cead` | Retained conflict-resolved form |
| Corrected surface-download scheduling | `f15a7da673d78ebb889a197cefdbd3b7904e0a6f` | Retained corrected successor |
| Active-MMU dirty reset | `93a4d66ac6ed3f01cacc80e2a1fc88daec26f2fa` | Retained |
| Texture pipeline fast path | `1a5e2aa87f497b89f787023f3a2c098b93697c37` | Retained |
| Corrected transient-buffer growth | `629411e183d382963bfcec5b21c3d6e31fd2358d` | Retained corrected successor |
| Uniform-row deduplication and VMState repair | `c67e2f4666a56701075c60d34853784bc04cb4ec` | Retained repaired form |
| Vertex upload-range reduction | `a62558309466b572ba1b797de3bf0a99602fc468` | Retained |
| Vulkan display conversion reuse | `8e4fa91858861c760afb51a71b5fa396a9a7b768` | Retained |
| Exact Windows QPC scaling fast path | `abb8a82485faa096d1e168ca67b6302b0816d59c` | Retained |
| PTIMER zero-ratio, post-load, and overdue-alarm reconciliation | Existing Full-Speed lineage | Retained |
| Callback retirement and exclusive-flush ordering | Existing Full-Speed lineage | Retained |
| Opt-in retail/CPU/Vulkan attribution | `e9e9e5d08df79a88162610042f3024a040964e7d` and existing telemetry lineage | Retained as diagnostic-only code |

This upgrade adds or repairs the following code on top of that existing head:

| Upgrade | Full-Speed commit | Result |
| --- | --- | --- |
| DSP snapshot-load quiescence | `ae9b0877a8` | Worker is idle before VMState replacement and resumes only after restored state reaches the backend |
| Bordered texture storage contract | `c35349365a` through `7b6a3216e5` | Source footprint, cache identity, image extent, route selection, copy bounds, mip layouts, and inclusive DMA bounds agree |
| Report DMA ownership and finish validation | `131748e96d` through `93d01dc984` | Immutable queue-time descriptor, complete descriptor/report bounds, supported class/target checks, and dedicated REPORT wait attribution |
| PFIFO inline capacity hardening | `7a253ece09` | Guest-controlled appends use Release-safe checks, reject atomically, and raise DATA_ERROR instead of relying on assertions |
| Ordered vertex staging v2 | `4dae5a9b71` | Ordered copies replace repeated vertex-dirty finishes when safe, with checked capacity and direct-copy fallback |
| Vulkan scratch arithmetic hardening | `3e5f370fce` | Checked add/multiply/alignment/growth and failure-safe texture/surface reservations |
| Combined texture integration build fix | `ccdb31d508` | Restores the boolean upload failure contract and removes a duplicate format declaration exposed by the exact rollup build |

Ordered vertex staging is the main current performance target: replace roughly
102 `VERTEX_BUFFER_DIRTY` waits in a representative Morrowind frame with copies
ordered in the draw command stream, while retaining a bounded 16 MiB staging
limit and an idle-safe fallback for incompatible or oversized updates.

## Deliberately excluded or reverted

Known-bad or unproven refs were not pulled in merely because they had a
`validate/` or feature name.

| Candidate | Disposition |
| --- | --- |
| PFIFO bulk method batching | Reverted; its guest-controlled fixed arrays relied on assertions. The scalar path is retained and hardened. |
| OpenGL native S3TC fast path | Reverted; bordered cubemap mip fallback used incorrect dimensions. OpenGL keeps the known decoded path. |
| Batched auxiliary/main Vulkan submit | Reverted; dependency repair was sound but the experiment measured neutral to slower. |
| Persistent NVIDIA P-state policy and migration | Excluded. Full-Speed adds no P-state mutation or migration, and lab runs disable xemu's separate generic NVIDIA profile setup. |
| “Reduce host load” timing option | Removed and excluded because it changed timing behavior and was identified as unsafe. |
| Stale transient-growth and surface-download refs | Excluded; the corrected successors listed above are already present. |
| Contaminated static-TCG validation stack | Not imported as a branch; only the already-reviewed isolated functionality in the ongoing lineage is retained. |
| Ordered-vertex v3 marker overlay | Excluded from production; v2 contains the functional change and tests. |
| TCG dirty-range inline experiment | Deferred until its isolated performance gate is complete. |
| Synchronous ETW/file telemetry as performance evidence | Telemetry remains opt-in for attribution, but enabled runs are diagnostic and are not used as unbiased A/B timing results. |

## Reproducible Windows Release build

The release is built on an x86-64 Linux host with Docker or another compatible
OCI runtime. Source comes from this Git repository and its pinned submodules.
The only package installed into the ephemeral container is `curl`, required by
xemu's versioned DSP dependency download during configuration.

Use this public xemu toolchain image by immutable digest:

```text
ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2
```

It supplies GCC 16.1 and the static MXE Windows toolchain. The Full-Speed
release uses 32 jobs, x86-64-v3, and full LTO. Check out the exact source commit
from the release manifest, then run:

```bash
git submodule update --init --recursive
test -z "$(git status --porcelain=v1)"
mkdir -p .build-cache/ccache .build-cache/lto

docker run --rm \
  -e CROSSPREFIX=x86_64-w64-mingw32.static- \
  -e CROSSAR=x86_64-w64-mingw32.static-gcc-ar \
  -e CCACHE_DIR=/xemu-cache/ccache \
  -e CCACHE_MAXSIZE=512M \
  -e LTO_CACHE_DIR=/xemu-cache/lto \
  -e BUILD_JOBS=32 \
  -v "$PWD:/src" \
  -v "$PWD/.build-cache:/xemu-cache" \
  -w /src \
  ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2 \
  bash -lc 'apt-get update && apt-get install -qy curl && \
    ./build.sh -j"$BUILD_JOBS" -p win64-cross \
      --extra-cflags="-flto-incremental=$LTO_CACHE_DIR -flto-partition=cache" \
      -Db_lto=true -Dx86_version=3'
```

`x86_64-w64-mingw32.static-gcc-ar` is intentional. Plain `ar` does not load
the GCC LTO plugin and caused unresolved LTO symbols in earlier builds.

The release ZIP contains the complete packaged build folder, including:

```text
xemu.exe
LICENSE.txt
build.log
build-info.txt
source-status.txt
SHA256SUMS.txt
focused-tests/
```

`build-info.txt` records the exact source commit, branch, toolchain digest,
compiler, build command, job count, CPU target, LTO mode, and source-bundle
hash. `SHA256SUMS.txt` uses relative paths and must verify after download:

```bash
sha256sum -c SHA256SUMS.txt
```

The executable retains DWARF information. If a PDB is needed, use public
[`cv2pdb` 0.52](https://github.com/rainers/cv2pdb/releases/tag/v0.52). Never
resolve an address with symbols from a different executable.

The current candidate build identity is:

```text
production source: ccdb31d508c2c929d839b05a1d2717b5b59868c4
xemu.exe SHA-256: 43e037d9bafc464486cdbee59fac8468a10ac41e91852eaf851a22e677b07b13
Full-Speed-ccdb31d508-Release.zip SHA-256: fbe0a7245d66c8d7941ff6cc989b25cc79045780031a094d4c0752bada7c3b0d
build result: PASS (Win64 Release, full LTO, x86-64-v3)
runtime result: BLOCKED pending test-runner HDD safety repair and re-audit
```

## Validation completed before publication

The combined source passed a clean-tree and whole-range `git diff --check`.
Its component repairs also have the following focused evidence:

| Area | Evidence |
| --- | --- |
| Report DMA | 4/4 focused tests and a complete Linux `qemu-system-i386` build |
| PFIFO capacities | Normal, `-DNDEBUG -O2`, and ASan/UBSan focused suites; exact-capacity, capacity+1, overflow, and atomic rejection |
| Ordered vertex | Modified renderer objects compiled with GCC 16.1/x86-v3/LTO; vertex 5/5, texture 6/6, Vulkan BC 3/3, uniform 6/6 under Wine |
| Vulkan scratch | Complete Win64 full-LTO/x86-v3 component build and 3/3 checked-arithmetic tests under `-DNDEBUG` |
| Texture contract | Swizzled mip, cache/source-layout, and inclusive DMA-boundary regression coverage retained in-tree |

The exact combined Release build passed. Runtime OpenGL/Vulkan campaigns are
not claimed here: the independent lab disk/snapshot isolation review found
remaining safety blockers after an earlier runner overwrote shared HDD state.
This prevents infrastructure faults or destructive execution from being
reported as emulator performance.

## Required runtime campaign

Testing is Release-only and is walked one branch at a time. Both renderers are
required because the XISO is a multi-renderer correctness tool.

For each candidate:

1. Run the 149-result performance-test XISO twice in OpenGL and twice in
   Vulkan; preserve each run and report the average without hiding soft fails.
2. Run Morrowind for one marker-bounded 60-second window from the backed-up
   heavy-area snapshot in OpenGL and Vulkan.
3. Run PGR2 for one marker-bounded 60-second window from a fresh reset in
   OpenGL and Vulkan. Do not use the snapshot as the performance control.
4. Record FPS change versus the named control, median/p95/p99/max frame time,
   CPU use, process RAM, GPU use, and VRAM use.
5. Keep xemu foreground and unobscured. Monitoring windows must not steal
   focus or cover the game window.
6. Set `display.setup_nvidia_profile=false`; testing must not alter NVIDIA
   profiles.
7. Store the exact executable/source hashes, configuration, XISO hash, HDD and
   snapshot identities, renderer, driver, OS, host hardware, and marker bounds
   with the results.

PGR2 automation starts after an emulator reset. If the BIOS animation is
allowed to play, wait 10 seconds first, then issue this sequence with the delay
after each key shown in seconds:

```text
A-3, A-10, A-2, A-2, F-2, A-2, A-2, A-2, A-2, A-2, A-7
```

The comparison order is:

```text
Official U -> correctness fix 1 -> correctness fix 2 -> ...
Stable S   -> performance patch 1
Stable S   -> performance patch 2
...
Official U -> final combined correctness branch
Stable S   -> final combined Full-Speed branch
```

The baseline need not be resampled between every isolated candidate, but it is
rerun at the end to detect host drift. A branch that crashes or fails a gate is
marked for repair and testing proceeds to the next independent candidate. No
combined result is used to claim that an isolated patch improved performance.

## Evidence standard

Counts and samples are leads. A performance finding must identify the guest
event, repeatable lost milliseconds, responsible running/ready/waiting path,
frequency, control comparison, required correctness invariant, and the metric
that should change. Small wins remain useful, but they are recorded at their
actual size and are not promoted without repeatable evidence.
