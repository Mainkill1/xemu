# GitHub PR 8 rising-demand qualification

Status: **PASS**

- Parent source: `54e557d4dcb88d72b687ee578c4247e86350f81b`
- Candidate source: `33151b41fda28dc318da777fac12568cffa550c7`
- Test source: `613ddbcf362c18c1380ca858185dc9fefeef5e4a`
- Test PR: [Mainkill1/xemu-perf-tests#5](https://github.com/Mainkill1/xemu-perf-tests/pull/5)
- Test: `vertex_buffer_allocation.rising_transient_growth`

## Result

The exact-growth policy remains correct across increasing demand. The focused workload reaches just below and exactly the 8 MiB initial capacity, adds three 128-byte increments, then adds about 2 MiB. Each growth crosses a submission boundary.

| Check | Result |
|---|---|
| Parent Vulkan output | PASS |
| Candidate Vulkan output | PASS |
| Parent OpenGL output | PASS |
| Candidate OpenGL output | PASS |
| Candidate production-path probe | PASS |
| Vulkan validation | 0 unique VUIDs in all three Vulkan cells |
| Mapping after growth | Restored after every growth |
| Paired-buffer capacity | Matched after every growth |
| Draw output | Identical hashes in all five cells |

All cells produced framebuffer FNV-1a64 `377e6c029807f325`, work checksum `7ecc995e`, result checksum `f36e0ec5`, and tile-center KAT `f36e0ec5`.

## Production allocation trace

The probe records the real transient-buffer request and resize path. It is enabled only in the probe build with `XEMU_TRANSIENT_BUFFER_GROWTH_TEST`; the parent and candidate Release builds do not contain it.

| Property | Observed |
|---|---:|
| Initial capacity | 8,388,608 bytes |
| Just-below request | 8,388,096 bytes |
| Exact-capacity request | 8,388,608 bytes |
| Growth 1 | 8,388,736 bytes |
| Growth 2 | 8,388,864 bytes |
| Growth 3 | 8,388,992 bytes |
| Growth 4 | 10,486,016 bytes |
| Submission counts at growth | 47, 48, 49, 50 |
| Resize events | 8: device and staging buffers for four growths |

The staging buffer was mapped again after every resize. The device/staging capacities stayed equal, the device buffer stayed unmapped, and every resize occurred outside active primary and auxiliary command buffers.

## Focused cells

| Role | Renderer | Outcome | One-shot runtime |
|---|---|---|---:|
| Parent | Vulkan | PASS | 877,253 us |
| Candidate | Vulkan | PASS | 939,981 us |
| Parent | OpenGL | PASS | 836,334 us |
| Candidate | OpenGL | PASS | 887,701 us |
| Candidate probe | Vulkan | PASS | 896,847 us |

These one-iteration cells include forced completion and serve only as correctness/allocation evidence. They are not performance measurements.

## Existing performance and resource proof

The exact candidate's earlier retail and focused qualification remains applicable. This follow-up does not replace those measurements.

| Workload / metric | Parent | Candidate | Delta |
|---|---:|---:|---:|
| PGR2 display-write cadence | 19.702/s | 19.789/s | +0.44% |
| PGR2 p95 interval | 66.752 ms | 65.176 ms | -2.36% |
| PGR2 p99 interval | 72.969 ms | 71.859 ms | -1.52% |
| Morrowind display-write cadence | 18.643/s | 18.792/s | +0.80% |
| Morrowind p95 interval | 65.764 ms | 62.411 ms | -5.10% |
| Morrowind p99 interval | 67.762 ms | 67.729 ms | -0.05% |
| PGR2 working set | 2,257.184 MiB | 985.563 MiB | -56.34% |
| PGR2 private memory | 7,113.281 MiB | 4,569.103 MiB | -35.77% |
| PGR2 device memory | 3,742 MiB | 2,470 MiB | -33.99% |

Existing unfavorable observations remain visible: focused inline-elements median was +0.95%, its p95 was +1.08%, PGR2 mean host CPU was +1.79%, and peak host CPU was +9.28%. These do not negate the demonstrated 34-73% memory reduction, correct output, or retail progression.

## Reproduction

Build the XISO from test source `613ddbcf362c18c1380ca858185dc9fefeef5e4a` with NXDK `73c95900965a16be3a3e34b8d4d5d41bc18498be`, then run `Vertex buffer allocation::XemuRisingTransientBufferGrowth` once per build and renderer. The XISO SHA-256 is `7f6fe704418f3d321e94aefbb07de4c0a2b92b569d64d2d00dd46a7c170fd6c4`.

Apply `pr8-growth-probe.diff` to candidate `33151b41fda28dc318da777fac12568cffa550c7`, build with `-DXEMU_TRANSIENT_BUFFER_GROWTH_TEST`, set `XEMU_TRANSIENT_BUFFER_GROWTH_TEST_OUTPUT` to a writable CSV path, run the same Vulkan test, then execute:

```text
python3 verify-transient-buffer-growth.py candidate-vulkan-growth-events.csv
```

The verifier has a synthetic negative control in its development record: a deliberately broken growth sequence is rejected.
