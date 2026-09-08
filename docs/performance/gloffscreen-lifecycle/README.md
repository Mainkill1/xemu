# ENG-2026-523: SDL/OpenGL offscreen lifecycle repair

## Qualification status

**Ready for review.** The S-based implementation `b16291b63157f1550f92f34725369f3c091e0108` passed the focused lifecycle regression, a matched full XISO suite on both renderers, fresh-start PGR2 on both renderers, and the Morrowind snapshot route on both renderers.

The change fixes a resource leak. It makes no performance-improvement claim.

## Problem and fix

OpenGL startup creates two durable shared contexts and one temporary GPU-properties context. `glo_context_destroy()` detached and freed only its wrapper. The owned SDL GL context and hidden SDL window survived until process termination.

```mermaid
flowchart LR
    A[Create temporary shared context] --> B[Probe GPU properties]
    B --> C[Detach current context]
    C --> D[Destroy SDL GL context]
    D --> E[Destroy hidden SDL window]
    E --> F[Free wrapper]
    F --> G[Restore durable display context]
```

The repair calls `SDL_GL_DestroyContext()` and `SDL_DestroyWindow()` before freeing the wrapper. Null destruction and shared-object lifetime remain covered by the focused test.

## Source and build identity

| Item | Identity |
| --- | --- |
| Base | `208e4596832a1f949bd63822d5ddd962c4575067` |
| Tested implementation | `b16291b63157f1550f92f34725369f3c091e0108` |
| Tested tree | `b0d4b43ff963710ee5da5103de0470f6a2885bb7` |
| Build | Windows Release, full LTO, x86-64-v3 |
| Toolchain | `ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2` |
| Candidate executable SHA-256 | `643b17cb84dad2ecc4708a10694349cfb88d526d3f4571ed230080cf4ab5eb1f` |
| Base executable SHA-256 | `6c41b50acb94fc71442ae6f217c8c236ffcc3fda12ce81e7553cb391b1fd2527` |

The `sdl.c` file at the old Full-Speed negative control and cumulative S base is byte-identical. The documented old-code test failed on the first leaked window. The repaired exact S head passed 64 temporary-context create/destroy cycles, preserved a texture in the durable shared context, returned to the original SDL window count after every cycle, and completed final cleanup.

## Test suite summary

| Test group | Vulkan | OpenGL | Evidence |
| --- | ---: | ---: | --- |
| Focused SDL/OpenGL lifecycle | PASS, 64 cycles | Same shared-context test | [unit output](evidence/gloffscreen-unit.json) |
| Matched full XISO | PASS, 147/147, 0 VUID | PASS, 147/147 | [receipt](evidence/xiso-receipt.json) |
| GameLoadComposite groups | 3/3 PASS | 3/3 PASS | Per-test CSVs below |
| Vulkan memory-pressure groups | 2/2 PASS | 2/2 PASS | Per-test CSVs below |
| PGR2 fresh start | PASS | PASS | [candidate receipt](evidence/pgr2/candidate-receipt.json) |
| Morrowind snapshot resume | PASS | PASS | [candidate receipt](evidence/morrowind/candidate-receipt.json) |
| Process/private-HDD cleanup | PASS | PASS | Retail receipts above |

Full per-test results: [Vulkan CSV](evidence/xiso-vulkan/suite-results.csv) and [OpenGL CSV](evidence/xiso-opengl/suite-results.csv).

## Retail performance summary

Each PGR2 cell used the fixed fresh-start route and a 60-second measurement. Morrowind used the fixed snapshot, Start/B resume sequence, and a 10-second measurement. The Morrowind rate is an NV2A display-write cadence proxy, not rendered or presented FPS.

| Test | Metric | S base | Candidate | Change |
| --- | --- | ---: | ---: | ---: |
| PGR2 Vulkan | Guest FPS | 30.010 | 30.016 | +0.02% |
| PGR2 Vulkan | p95 interval | 38.495 ms | 40.388 ms | +4.92% |
| PGR2 Vulkan | p99 interval | 41.343 ms | 42.264 ms | +2.23% |
| PGR2 OpenGL | Guest FPS | 30.010 | 29.987 | -0.08% |
| PGR2 OpenGL | p95 interval | 39.948 ms | 39.635 ms | -0.78% |
| PGR2 OpenGL | p99 interval | 42.795 ms | 42.181 ms | -1.43% |
| Morrowind Vulkan | Display-write cadence | 27.462/s | 27.998/s | +1.95% |
| Morrowind Vulkan | p95 interval | 44.833 ms | 42.607 ms | -4.97% |
| Morrowind Vulkan | p99 interval | 51.057 ms | 52.436 ms | +2.70% |
| Morrowind OpenGL | Display-write cadence | 37.472/s | 36.793/s | -1.81% |
| Morrowind OpenGL | p95 interval | 33.447 ms | 33.246 ms | -0.60% |
| Morrowind OpenGL | p99 interval | 37.708 ms | 38.462 ms | +2.00% |

These are single paired cells used as a regression screen. The mixed small movements, including unfavorable Vulkan tail rows, do not establish a performance change. They are retained rather than relabeled as improvements.

## Resource summary

| PGR2 metric | Vulkan base | Vulkan candidate | OpenGL base | OpenGL candidate |
| --- | ---: | ---: | ---: | ---: |
| Process CPU, host capacity mean | 21.915% | 21.870% | 22.101% | 22.980% |
| Process GPU engine sum mean | 44.122% | 41.072% | 44.529% | 44.797% |
| Working set mean | 1005.8 MiB | 995.98 MiB | 678.95 MiB | 684.45 MiB |
| Private bytes mean | 3117.0 MiB | 3101.9 MiB | 2337.0 MiB | 2333.1 MiB |

No board-power collector was enabled for this lifecycle qualification. Resource measurements are one run per cell and are reported as observations, not causal performance findings.

## Known inherited failure in the expanded XISO

The first expanded 152-record Vulkan run reached the bordered-cubemap case, emitted six `VUID-vkCmdCopyBufferToImage-imageSubresource-07971` messages, lost the device, and left incomplete JSON. The runner rejected the cell and stopped the queue. This failure is inherited from cumulative S and is repaired by the later Vulkan bordered-cubemap series; it is unrelated to the two SDL destruction calls in this PR.

The matched 147-record suite was then used as the bounded workaround and passed both renderers. The failed attempt remains public in [its receipt](evidence/xiso-new-known-failure/receipt.json) and [summary](evidence/xiso-new-known-failure/summary.json).

## Evidence inventory

- [Build receipt](evidence/build-receipt.json)
- [Focused lifecycle output](evidence/gloffscreen-unit.json)
- [Matched XISO receipt](evidence/xiso-receipt.json)
- [PGR2 base receipt](evidence/pgr2/base-receipt.json)
- [PGR2 candidate receipt](evidence/pgr2/candidate-receipt.json)
- [Morrowind base receipt](evidence/morrowind/base-receipt.json)
- [Morrowind candidate receipt](evidence/morrowind/candidate-receipt.json)

All successful GUI cells ran through the Session 1 PowerShell pipe. All retail cells left no xemu process; all private HDDs were deleted and their immutable seeds remained unchanged.
