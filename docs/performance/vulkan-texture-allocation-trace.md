# Vulkan texture allocation trace

This diagnostic measures the actual VMA image creation/destruction calls for
ordinary texture-cache images. It helps investigate issue #246. It does not
retain images, change allocation policy, or establish a performance improvement.

## Run and read

Set `XEMU_VK_TEXTURE_ALLOCATION_LOG` to a **new writable file path** before
starting xemu with the Vulkan renderer. An unset or empty variable disables the
trace. Creation is exclusive: an existing file is refused and preserved. The
runner must collect the file after orderly emulator exit.

```sh
python3 scripts/performance/vk-texture-allocation-summary.py trace.jsonl --out summary.json
```

The output path must also be new. Python 3.11 or later is required for full-file
hashing. The finalized reader verifies sequence, configuration types, handle
generations and footer accounting, then hashes the original file. Schema 2
distinguishes a complete renderer teardown from an idle shutdown checkpoint.
Teardown requires balanced lifetimes; a checkpoint reports images/bytes still
live without inventing destruction events. Truncation, missing footer, dropped
records and clock errors invalidate either report. A failed allocation remains an event with its
original Vulkan return code; it is never retried by the trace.

For a planned runner diagnostic while xemu is still running:

```sh
python3 scripts/performance/vk-texture-allocation-summary.py trace.jsonl --schema-only
```

This reads one bounded header. It does **not** validate the lifecycle, predict
the final outcome, or report allocation totals. Each input record is limited to
65,536 characters. The production writer emits ASCII JSON.

## Scope and units

- Ordinary texture-cache images include texture copies of render surfaces.
  Dummy textures, renderer-owned surfaces and unrelated allocations are excluded.
- `elapsed_us` brackets `vmaCreateImage` or `vmaDestroyImage` using GLib's
  monotonic host clock. It is **host elapsed microseconds**, not CPU time,
  guest time or allocation-only internals. Reader totals use milliseconds.
- Metadata queries and JSON serialization happen outside those timing brackets.
  Their cost and the complete observer cost require independent OFF/ON testing.
- `frame` counts completed renderer flip stalls; it is not a guest frame number.
  `submission` is the existing renderer submission counter. `start_us` is the
  host monotonic clock value, not time since process launch.
- `size_bytes` comes from `vmaGetAllocationInfo`. Created bytes count allocation
  generations; peak live bytes cover traced images only, not the device heap,
  resident VRAM or VMA's complete budget.

The renderer owns the trace and calls it serially. At shutdown the existing
`nv2a_lock_fifo` handoff waits for FIFO idle and holds PFIFO and PGRAPH locks;
its pre-shutdown callback closes and detaches the trace while the producer is
quiescent. It adds no lock or resource destruction. There is no polling thread,
per-binding counter or atomic publication. The 100,000-event cap bounds output.
After the cap, calls still execute and aggregate totals continue, but dropped
events invalidate lifecycle analysis. Output/clock errors are diagnostic
failures; they do not alter the original allocation result or destroy operation.

## Configuration matching

Creation records capture the actual Vulkan image description after existing
extent adjustments and surface scaling, all allocation-create fields (including
`minAlignment` and the raw priority bits), actual allocation size and memory
type. Unknown `pNext`, queue-family arrays, custom pools or user data mark a
configuration unsupported for matching.

The reader counts matching ordinary creations that follow a compatible
destruction. Surface copies and unsupported configurations are excluded.
These are **unbounded reuse opportunities**, not pool hits: matching ignores
retention limits, memory pressure, fences and the cost of maintaining a pool.
It cannot prove that retaining or reusing an image is safe. Teardown destroys
do not replenish the opportunity ledger.

## Execution flow

```text
Before: ordinary create/destroy -> unchanged VMA call
After, trace OFF: wrapper -> unchanged VMA call
After, trace ON: start clock -> unchanged VMA call -> end clock
                -> allocation metadata on successful creation -> bounded JSON event
Renderer finalization: mark teardown -> existing texture destruction -> teardown footer
Normal process shutdown: existing idle PFIFO/PGRAPH handoff -> checkpoint footer
                         -> detach trace; resource ownership remains unchanged
```

The disabled wrappers preserve original parameters and results and perform no
clock reads, allocation metadata queries or output. They still introduce helper
boundaries and pointer checks; their native performance cost is unqualified.

## Qualification and limitations

Use matched parent and candidate release builds on the same host, with fixed
assets, settings, renderer and qualified cache state. First compare parent
against candidate with trace OFF, then compare OFF/ON using the **same candidate
executable**. Use A/A controls and balanced physical ABBA/BAAB ordering. Keep
profiler use identical between compared runs. Preserve original failures.
Report per-leaf XISO correctness/timings, representative gameplay frame costs
and variability separately from allocation-call durations.

The current trace covers one Vulkan renderer observation window. Normal QEMU
cleanup does not unrealize NV2A, so a process-exit checkpoint cannot establish
that all resources were destroyed or measure their later destruction cost.
Finalization through renderer teardown remains a separate end reason. Reset or backend
reactivation with the same path refuses the existing file; those sequences are
not qualified. Abnormal termination may omit teardown/footer and is rejected.
The trace does not yet attribute LRU/in-flight exclusions, upload bytes, trim
reasons, OOM recovery or complete heap pressure. Existing VMA budget querying
and pressure-triggered trimming are compiled out. A pool requires a separate,
tested pressure drain and lifetime policy.

Local unit tests use a fake VMA boundary to check forwarding and accounting;
their recorded durations are not native performance measurements.
