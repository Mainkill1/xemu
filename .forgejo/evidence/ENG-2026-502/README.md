# ENG-2026-502 direct SSE evidence

Status: **research candidate; not accepted for the PR train**.

The focused `CpuFloatingPoint::SSEScalar` workload proves that direct scalar
lowering can reduce the tested path cost. Retail Morrowind testing found a
large guest-FPS regression in the broad implementations, so each revision is
retained below instead of treating the microbenchmark win as production proof.

## Build matrix

| Build | Source | Focused result | Retail result | Gate |
| --- | --- | --- | --- | --- |
| `eng502-direct-sse-gcc-20260901` | `249e9b2d48` | not retained | not run | unverified |
| `eng502-direct-sse-r2-gcc-20260901` | `25c208cad9` | 2.785x SSE throughput; exact hash | not run | full ISO passed; retail pending |
| `eng503-sse-flip-gcc-20260901` | `d7edf49c60` over `25c208cad9` | inherited | PGR2 26.17698; Morrowind 20.10244 FPS | rejected: Morrowind -12.49% |
| `eng504-baseline-flip-gcc-20260901` | `d6b1fe823fec` over `df0a730736` | control | PGR2 26.44401; Morrowind 22.97103 FPS | control; first PGR2 restore failed |
| `eng505-sse-scalar-gcc-20260901` | `9b021e16e2` | 2.80x SSE throughput; exact hash | not run | pending |
| `eng506-sse-scalar-flip-gcc-20260901` | `c0245e2c79` | inherited | Morrowind 20.13241 FPS | rejected: regression remains |
| `eng507-sse-control-cache-gcc-20260901` | `0bb6afd811` | SSE exact; X87 exact without paired speed control | not run | pending |
| `eng508-sse-control-cache-flip-gcc-20260901` | `bd7f1a0570` | inherited | Morrowind 20.24707 FPS | rejected; first restore failed |
| `eng509-sse-guarded-gcc-20260901` | `80dd00b1c6` | 1.918x SSE throughput; exact hash | not run | pending guarded retail test |
| `eng510-sse-guarded-flip-gcc-20260901` | `20d92b60fa` | inherited | not run | unverified diagnostic |

## Vulkan/OpenGL control pair

The exact ENG504 control was also captured on both backends. Morrowind measured
22.97103 FPS on Vulkan and 42.34442 FPS on OpenGL (`+84.34%`). PGR2 measured
26.44401 FPS on Vulkan and 25.13740 FPS on OpenGL (`-4.94%`). This identifies a
large scene-specific Vulkan deficit without claiming that OpenGL is universally
faster. Full report: `J:\xemu-lab-working-set\active\reports\eng511-vulkan-opengl-shared-hotpaths.md`.

## Exact control and focused proof

- Base: `df0a73073655508af1a2bfd5c66019f3e84ea236`.
- Baseline SSE total: 49.843488 s; median 498504 us.
- Broad candidate total: 17.897511 s; median 179044 us.
- Guarded candidate total: 25.984 s; median 259887.5 us.
- Expected framebuffer hash: `6962077c244da325`; all recorded focused
  candidates matched it.
- The `eng502-r2` Vulkan 1x full ISO completed 141 records and passed its
  functional hashes.

## Retail interpretation

The exact control and candidate use the same `XEMU_FLIP_LOG` probe. PGR2 was
nearly unchanged, while Morrowind fell from 22.97103 to 20.10244 FPS. Removing
packed direct lowering and caching host FP controls did not recover the loss.
ETW module weights differed by only about one percent, so the observed FPS loss
is treated as changed guest FP behavior, not a twelve-percent host CPU cost.

The guarded revision keeps SoftFloat for non-default MXCSR, zero, subnormal,
infinite, NaN, and non-normal result cases. It remains unaccepted until the
paired retail run and special-value/MXCSR tests pass.

## Evidence locations

- Durable report: `J:\xemu-lab-working-set\active\reports\eng502-direct-sse-ab.md`.
- Suite proof: `C:\xemu-lab\runs\suite\2026-09-01-143940-perf-full-suite-unknown`.
- Retail captures: `C:\xemu-lab\captures\pgr2-wpr` and
  `C:\xemu-lab\captures\morrowind-wpr` using the run IDs listed in each build
  record.
