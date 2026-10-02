# Issue #246: allocation collector local validation

This packet validates the opt-in Vulkan texture allocation collector and its
reader. It contains **no Steam Deck candidate measurement or performance gain**.
The commit containing this packet identifies the candidate; the exact tested
source hashes are in [source-inventory.json](source-inventory.json). Parent:
`ee5ce48b48784f999af374c1452003f8b2b1230f`. The earlier five parent-only native
attempts remain in the linked preflight packets and are not candidate evidence.

## Decision: HOLD

| Check | Result | Meaning |
|---|---|---|
| Local GCC xemu build | Passed | Collector integrates into the emulator; not a matched native release build. |
| Focused GPU unit tests | 4 tests, 26 subtests passed | Binding 6, BC layout 4, collector 8, reports 8. |
| New allocation reader | 10 tests passed | Lifecycle/configuration integrity, bounded input, output preservation. |
| Existing schema-8 reader | 11 tests passed | Retained tool coverage. |
| Production C writer to Python reader | Passed | Two create/destroy generations, balanced lifetimes; fake VMA fixture, not a benchmark. |
| Full unit suite | **Build failed; tests did not run** | Unchanged resampler test lacks declarations for `sinf`/`cosf`. |
| Exact-parent resampler compile | **Same failure reproduced** | Does not excuse the incomplete full-suite gate. |
| Native OFF/ON overhead, matched parent comparison, XISO timings | Not run | No performance acceptance or pool safety claim. |

Fresh verification commands and their exit codes are in
[verification.json](verification.json). The broad-suite attempt exited 125;
its exact-parent compilation control exited 1. The unchanged resampler source
was verified against the parent before compiling with the same flags. No
unrelated source fix, outcome repair or hidden retry was used.

Collector tests exercise disabled forwarding, actual post-adjustment creation
configuration, failed allocations, unsupported keys, teardown totals, null
destruction, output cap and existing-file protection. Reader tests independently
check every configuration field, actual memory identity, handle generations,
surface-copy exclusions, corrupt accounting and incomplete input. Clock/I/O
fault injection and native renderer shutdown/reset coverage are not established.

Independent read-only review found no correctness findings. It assessed source
and Python checks, not native behavior, observer overhead or future pooling.

## Raw records

[records.tar.gz](records.tar.gz): 26 text/JSON records, 41,952 bytes.
SHA-256: `2d9405ae3a380d1e02674ebdbfe03a8ae2147efbfaa7f30c04adaefc33f3e428`.
[records-inventory.json](records-inventory.json) lists original byte sizes and
SHA-256 values. The archive retains initial failing tests, the unsupported-key
and metadata regressions, successful checks, configure/build logs, the broad
suite build failure and the parent control. It includes the real production
writer's fake-boundary JSONL and reader report. No executable, firmware, disc,
HDD or cache is published.

The collector's durations bracket VMA calls in host elapsed microseconds.
Metadata/output costs are outside those brackets. Even accurate observed call
durations cannot establish a useful optimization without matched native
controls and representative repeated churn. The offline reader's compatible
post-destruction creations are unbounded opportunities, not safe pool hits.

## Next gates

On Steam Deck `10.0.0.123`, qualify the current release candidate's complete
trace and scene first. Compare the same candidate executable OFF/ON and matched
parent/candidate with trace OFF, including A/A and physical ABBA/BAAB. Retrieve
affected and control XISO leaf timings/correctness for each backend. Add
transition/streaming and another texture-heavy workload before judging material
churn. LRU/in-flight reasons, upload-byte attribution and heap/pressure behavior
remain outside this collector. A retained-image candidate requires separate
lifetime, fence, memory-limit and pressure-drain qualification.

## Related material

- [Maintained trace usage and units](../../performance/vulkan-texture-allocation-trace.md)
- [Five parent attempts: original packet](../issue246-allocation-preflight-20261002/README.md)
- [Morrowind scene qualification](../issue246-morrowind-scene-preflight-20261002/README.md)
