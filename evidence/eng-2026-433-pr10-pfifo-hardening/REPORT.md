# PR #10 PFIFO packet hardening qualification

Status: **PASS**

- PR: [Mainkill1/xemu#10](https://github.com/Mainkill1/xemu/pull/10)
- Tracking issue: [Mainkill1/xemu#34](https://github.com/Mainkill1/xemu/issues/34)
- Test PR: [Mainkill1/xemu-perf-tests#6](https://github.com/Mainkill1/xemu-perf-tests/pull/6)
- Old head: `52e806ab30f5664c2814fa9038f0f7c8bca2f2de`
- Repaired head: `fa95400e158e1849f5ee80f16345c52968e1c44e`
- Test source: `aade4293b8d031217b1ba459b21213985bee152c`

## Problem found

The bulk `ARRAY_ELEMENT16`, `ARRAY_ELEMENT32`, and `INLINE_ARRAY` handlers enforced `NV2A_MAX_BATCH_LENGTH` with assertions. A guest packet crossing the boundary terminated a supported Release build. The exact old PR head reproduced this with exit code 3:

```text
Assertion failed: pgraph_inline_packet_fits(*length, num_words_available,
values_per_word, NV2A_MAX_BATCH_LENGTH)
```

For element packets, pending `DRAW_ARRAYS` expansion occurred before that assertion. An invalid packet could therefore flush/reset earlier draw state or append pending indices before xemu aborted. The packet was not rejected as one atomic operation.

## Repair

The dispatcher now calculates the whole resulting length before any state mutation. The calculation includes pending `DRAW_ARRAYS` values, handles two output values per `ARRAY_ELEMENT16` word, and checks subtraction/division before multiplication.

If the complete packet will not fit, xemu logs a guest error, consumes the malformed dispatch unit, and leaves inline buffers and pending draw arrays unchanged. Non-incrementing packets consume the full selected batch; incrementing packets consume one method word. When tracing selected the scalar path, the rejected packet's remaining method trace records are retained.

Valid bulk, scalar-trace, and incrementing handling remains unchanged.

## Focused result

| Path | Renderer | Cells | Result | Validation |
|---|---|---:|---|---|
| Old bulk boundary control | Vulkan | 1 | Expected assertion abort | Reproduced at exact old head |
| Repaired bulk boundary | Vulkan | 4 | 4/4 PASS | 0 VUIDs |
| Repaired bulk boundary | OpenGL | 4 | 4/4 PASS | Exact guest KAT hashes |
| Repaired scalar-trace boundary | Vulkan | 4 | 4/4 PASS | 0 VUIDs; actual `-trace` argument recorded |
| Repaired PGR2 representative | Vulkan | 2 | Bulk and scalar-trace PASS | Identical framebuffer hash |
| Instrumented production decisions | Vulkan | 3 | Bulk, scalar-trace, incrementing PASS | Exact consumption/state transitions |

The four fixed framebuffer hashes matched in every applicable renderer/path:

| Test | FNV-1a64 |
|---|---|
| `ARRAY_ELEMENT16` boundary | `4d1756526f052325` |
| `ARRAY_ELEMENT32` boundary | `6eb7071be692a325` |
| `INLINE_ARRAY` boundary | `847da1930526a325` |
| Incrementing inline fallback | `640439ee8ecd2325` |

The PGR2 packet representative produced `bbc8b0702ffd0325` in both bulk and scalar-trace modes.

The production-path probe observed the required terminal sequence at capacity 524,287 for both bulk and scalar-trace modes: reject the crossing word without changing length 524,286; accept one exact-tail word to 524,287; reject the next word without changing length. The incrementing packet exposed two available words, consumed exactly one, and increased inline length from zero to one.

These one-shot boundary timings are correctness evidence. Live guest markers were unavailable, so they are not used as performance measurements.

## Existing performance proof

The hardening runs before the existing valid-packet implementation and does not alter its loops. The prior exact PR-head measurements remain the performance proof for the bulk optimization; the new exact repaired-head tests qualify its malformed-input and scalar/bulk behavior.

| Workload / metric | Parent | Original PR head | Delta |
|---|---:|---:|---:|
| Focused inline arrays / average | 74,899 us | 73,503 us | -1.86% |
| PGR2 cadence | 29.808/s | 29.860/s | +0.18% |
| PGR2 p95 interval | 40.519 ms | 40.277 ms | -0.60% |
| PGR2 p99 interval | 42.731 ms | 41.956 ms | -1.81% |
| Morrowind cadence | 18.209/s | 18.644/s | +2.39% |
| Morrowind p95 interval | 62.257 ms | 61.721 ms | -0.86% |
| Morrowind p99 interval | 66.526 ms | 65.372 ms | -1.73% |

Visible unfavorable observations from that same comparison remain: PGR2 host-present p95 increased 1.14%, mean host CPU increased 1.24%, peak host CPU increased 5.98%, and process-GPU utilization increased 0.62%. PGR2 remained capped near 30 FPS, so higher power or utilization must be interpreted together with cadence and completed work. No new performance claim is made from the boundary runs.

## Reproduction

Build test source `aade4293b8d031217b1ba459b21213985bee152c` with NXDK `73c95900965a16be3a3e34b8d4d5d41bc18498be`. The XISO SHA-256 is `930e7424e1ce7a98da75ce970ea9691bc7650f34d18af6017e936992c8d110fd`.

Run each `PFIFOPacketBoundary` leaf against old and repaired official Windows Release/full-LTO/x86-64-v3 builds. Enable `nv2a_pgraph_method_abbrev` through xemu's `-trace` option for the scalar comparison. The old `ARRAY_ELEMENT16` run must abort at the assertion; the repaired runs must return one PASS record with the hashes above.

Apply `pr10-pfifo-probe.diff` only to repaired source, compile with `-DXEMU_PFIFO_PACKET_TEST`, and set `XEMU_PFIFO_PACKET_TEST_OUTPUT` to a writable CSV. Verify the public records with:

```text
python3 verify-pfifo-hardening.py
```

The verifier also rejected a deliberately corrupted terminal length in its development negative control.
