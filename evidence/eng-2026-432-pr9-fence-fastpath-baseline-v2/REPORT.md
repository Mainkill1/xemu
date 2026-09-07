# ENG-2026-432 PR08 baseline-v2 qualification

- Baseline: `d03cc91c0d5ee82ea10c9b4782042aeec0299ee1` / tree `78737c40c982b8fb7ace9fd7a06f539c4f497dc7` / xemu `29f25f9057b23f2bf539c43da469f202ae79924146036600676468d33873278c`
- Candidate: `67178d0af1e9b7e4a4019dd5c2c77e9045ae8b3b` / tree `3d7eeb113b81f449e31d9b8d713696d72c7b36d2` / xemu `8bdb01e0bcc932364f522d73e5b11cb9dc94c94203150230d8d1a6ff15a3b1a2`
- Candidate tree exactly matches validated cumulative baseline-v2 `208e4596`; baseline removes only the fence fastpath.
- Build: official Windows Release, full LTO, x86-64-v3.

## Performance Summary

| Workload / Metric | Renderer | Baseline | Candidate | Delta |
|---|---|---:|---:|---:|
| PGR2 / display-write cadence | OpenGL | 24.926 FPS | 30.019 FPS | +20.43% |
| PGR2 / p95 frame interval | OpenGL | 50.759 ms | 39.343 ms | -22.49% |
| PGR2 / p99 frame interval | OpenGL | 65.506 ms | 42.380 ms | -35.30% |
| PGR2 / display-write cadence | Vulkan | 23.641 FPS | 30.004 FPS | +26.92% |
| PGR2 / p95 frame interval | Vulkan | 55.523 ms | 39.427 ms | -28.99% |
| PGR2 / p99 frame interval | Vulkan | 62.151 ms | 41.965 ms | -32.48% |
| Morrowind / NV2A display-write cadence | OpenGL | 38.395 FPS | 38.348 FPS | -0.12% |
| Morrowind / p95 frame interval | OpenGL | 32.580 ms | 32.637 ms | +0.17% |
| Morrowind / p99 frame interval | OpenGL | 37.631 ms | 37.114 ms | -1.37% |
| Morrowind / NV2A display-write cadence | Vulkan | 27.380 FPS | 27.182 FPS | -0.72% |
| Morrowind / p95 frame interval | Vulkan | 43.683 ms | 43.623 ms | -0.14% |
| Morrowind / p99 frame interval | Vulkan | 51.620 ms | 52.068 ms | +0.87% |
| Matched XISO / leaf average sum | OpenGL | 24.169 s | 18.645 s | -22.85% |
| Matched XISO / leaf average sum | Vulkan | 16.922 s | 16.511 s | -2.43% |

Morrowind is one replacement-snapshot 10-second capture per build/renderer. PGR2 is two 60-second runs per build/renderer in ABBA order. Higher cadence and lower frame intervals are favorable. XISO timing is context because live guest markers were unavailable.

## Test Results

- Matched XISO: 4/4 cells PASS; 147/147 records per cell; Vulkan baseline and candidate both report 0 VUIDs.
- Morrowind: 4/4 cells PASS with progression, nonblank final images, seed integrity, and private-HDD cleanup.
- PGR2: 8/8 cells PASS with progression, nonblank final images, zero focus incidents, process cleanup, and private-HDD cleanup.

### Test Suite Summary

| Test group | Renderer | Tests | Baseline average sum | Candidate average sum | Average delta | Baseline median sum | Candidate median sum | Median delta | Result |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| BusyPfifo | OpenGL | 2 | 9,535,693 us | 4,601,683 us | -51.74% | 9,535,607 us | 4,601,686 us | -51.74% | Improve flag |
| CpuFloatingPoint | OpenGL | 2 | 1,239,088 us | 1,240,781 us | +0.14% | 1,238,003 us | 1,240,548 us | +0.21% | Neutral/context |
| CpuTranslationBlocks | OpenGL | 3 | 518,981 us | 518,493 us | -0.09% | 519,029 us | 519,137 us | +0.02% | Neutral/context |
| FillRate | OpenGL | 2 | 2,942 us | 2,682 us | -8.84% | 2,504 us | 2,501 us | -0.12% | Neutral/context |
| GameLoadComposite | OpenGL | 52 | 11,081,166 us | 10,466,112 us | -5.55% | 11,085,003 us | 10,467,568 us | -5.57% | Improve flag |
| High vertex count | OpenGL | 4 | 202,398 us | 207,451 us | +2.50% | 202,537 us | 208,041 us | +2.72% | Neutral/context |
| PFIFOArrayElements | OpenGL | 3 | 3,916 us | 3,708 us | -5.31% | 3,905 us | 3,811 us | -2.41% | Neutral/context |
| PipelineTextureSwitch | OpenGL | 4 | 55,047 us | 55,377 us | +0.60% | 54,733 us | 55,074 us | +0.62% | Neutral/context |
| PrimitiveType | OpenGL | 20 | 18,363 us | 17,664 us | -3.81% | 16,741 us | 16,560 us | -1.08% | Neutral/context |
| ReportQuery | OpenGL | 6 | 3,333 us | 3,515 us | +5.46% | 3,323 us | 3,458 us | +4.06% | Neutral/context |
| SurfaceRendering | OpenGL | 26 | 349,308 us | 344,029 us | -1.51% | 334,389 us | 331,371 us | -0.90% | Neutral/context |
| TinyDraw | OpenGL | 8 | 326,597 us | 327,448 us | +0.26% | 325,742 us | 326,932 us | +0.37% | Neutral/context |
| UniformThrash | OpenGL | 1 | 1,819 us | 1,585 us | -12.86% | 1,763 us | 1,479 us | -16.11% | Improve flag |
| Vertex buffer allocation | OpenGL | 9 | 830,431 us | 854,797 us | +2.93% | 835,550 us | 854,034 us | +2.21% | Neutral/context |
| BusyPfifo | Vulkan | 2 | 5,279,136 us | 4,978,597 us | -5.69% | 5,278,744 us | 4,978,133 us | -5.69% | Improve flag |
| CpuFloatingPoint | Vulkan | 2 | 1,238,588 us | 1,249,255 us | +0.86% | 1,237,924 us | 1,249,122 us | +0.90% | Neutral/context |
| CpuTranslationBlocks | Vulkan | 3 | 513,031 us | 523,314 us | +2.00% | 513,138 us | 523,108 us | +1.94% | Neutral/context |
| FillRate | Vulkan | 2 | 3,618 us | 3,626 us | +0.22% | 1,966 us | 1,988 us | +1.12% | Neutral/context |
| GameLoadComposite | Vulkan | 52 | 7,435,423 us | 7,343,853 us | -1.23% | 7,427,128 us | 7,350,321 us | -1.03% | Neutral/context |
| High vertex count | Vulkan | 4 | 223,364 us | 225,339 us | +0.88% | 222,055 us | 223,950 us | +0.85% | Neutral/context |
| PFIFOArrayElements | Vulkan | 3 | 2,900 us | 2,706 us | -6.69% | 1,941 us | 1,885 us | -2.89% | Neutral/context |
| PipelineTextureSwitch | Vulkan | 4 | 83,867 us | 81,957 us | -2.28% | 81,559 us | 79,795 us | -2.16% | Neutral/context |
| PrimitiveType | Vulkan | 20 | 17,041 us | 16,855 us | -1.09% | 12,927 us | 12,745 us | -1.41% | Neutral/context |
| ReportQuery | Vulkan | 6 | 17,240 us | 16,830 us | -2.38% | 16,162 us | 16,371 us | +1.29% | Neutral/context |
| SurfaceRendering | Vulkan | 26 | 1,045,628 us | 995,067 us | -4.84% | 999,730 us | 950,054 us | -4.97% | Neutral/context |
| TinyDraw | Vulkan | 8 | 175,003 us | 180,494 us | +3.14% | 174,219 us | 179,983 us | +3.31% | Neutral/context |
| UniformThrash | Vulkan | 1 | 2,672 us | 2,757 us | +3.18% | 1,986 us | 2,012 us | +1.31% | Neutral/context |
| Vertex buffer allocation | Vulkan | 9 | 884,380 us | 890,158 us | +0.65% | 889,005 us | 893,205 us | +0.47% | Neutral/context |

A timing flag requires average and median group sums to move at least 5% in the same direction. Signed changes alone do not determine acceptance.

## Correctness

| Check | OpenGL Baseline | OpenGL Candidate | Vulkan Baseline | Vulkan Candidate |
|---|---|---|---|---|
| Matched 147-record XISO | PASS | PASS | PASS | PASS |
| Functional hashes | PASS | PASS | PASS | PASS |
| Vulkan validation | N/A | N/A | 0 VUIDs | 0 VUIDs |
| Morrowind progression/image | PASS | PASS | PASS | PASS |
| PGR2 progression/image | PASS | PASS | PASS | PASS |
| Private HDD/process cleanup | PASS | PASS | PASS | PASS |

## Resource Impact

PGR2 values are arithmetic means of two matching 60-second runs.

| Metric | Renderer | Baseline | Candidate | Delta |
|---|---|---:|---:|---:|
| Host CPU mean (%) | OpenGL | 20.704 | 22.121 | +6.85% |
| Working set mean (MiB) | OpenGL | 674.907 | 677.913 | +0.45% |
| Private bytes mean (MiB) | OpenGL | 2336.484 | 2338.685 | +0.09% |
| Process GPU engines mean (%) | OpenGL | 43.720 | 44.506 | +1.80% |
| Dedicated GPU memory mean (MiB) | OpenGL | 210.153 | 209.160 | -0.47% |
| Device GPU mean (%) | OpenGL | 37.250 | 38.750 | +4.03% |
| Device memory mean (MiB) | OpenGL | 588.000 | 587.000 | -0.17% |
| Host CPU mean (%) | Vulkan | 17.609 | 21.662 | +23.01% |
| Working set mean (MiB) | Vulkan | 992.835 | 1001.087 | +0.83% |
| Private bytes mean (MiB) | Vulkan | 3101.321 | 3112.434 | +0.36% |
| Process GPU engines mean (%) | Vulkan | 44.379 | 42.732 | -3.71% |
| Dedicated GPU memory mean (MiB) | Vulkan | 674.086 | 674.938 | +0.13% |
| Device GPU mean (%) | Vulkan | 35.500 | 35.900 | +1.13% |
| Device memory mean (MiB) | Vulkan | 1028.000 | 1028.650 | +0.06% |

## Decision

**Integrate.** The candidate restores the reviewed fence fastpath on the cumulative baseline-v2 context, reaches PGR2’s 30 FPS cap on both renderers, preserves Morrowind progression with near-neutral cadence, and passes the exact matched XISO correctness gate.

Unfavorable observations remain visible: Morrowind Vulkan cadence decreased slightly and its p99 increased slightly; OpenGL cadence and p95 also moved slightly unfavorably. Resource use must be read together with the substantial increase in completed PGR2 guest work.

## Evidence

- `RUN-MANIFEST.json`
- `QUALIFICATION-SUMMARY.json`
- `pgr2-results.csv`
- `morrowind-results.csv`
- `xiso-group-summary.csv`
- `xiso-per-test.csv`
