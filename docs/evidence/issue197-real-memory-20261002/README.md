# Issue 197: actual memory remapping and retained reader benchmark

**Decision: HOLD.** The previous cached reader returns stale payload data after
a same-address RAM remap. A per-word FlatView identity check repairs this
synchronous case. Performance remains unqualified on the Deck.

## Correctness result

The SGE descriptor is real QEMU MMIO. Its second read replaces one real RAM
region with another at physical address `0x4000`, without changing the
descriptor value. Required output is `11223344`, `55667788`, `99aabbcc`.

| Reader | Result | Second word |
| --- | --- | --- |
| Original per-word physical loads | PASS | `55667788` |
| Candidate before view check | FAIL / assertion, exit −6 | `deadbeef` (stale) |
| Candidate with view check | PASS | `55667788` |

`original-control.txt` and `candidate-red.txt` preserve the semantic control
and failure. The artifact probe links existing local emulator objects from
the issue246 build; the reader comes from this PR's header. It is not a
matched performance build. `guard.patch` adds only the mapping-view check.
`stale-mapping-reader.h` preserves the exact unguarded header.

The first three probe executions failed during fixture initialization (missing
QOM machine/container, then null `current_machine`); their original logs are
retained. They are harness failures, not candidate failures. The fourth run
reached the stale-word assertion. No outcomes were repaired.

## Maintained test and measurement tool

`tests/xbox/mcpx-apu/test-sample-memory.c` now links the owning branch's actual
emulator objects through a Linux Meson target. Both candidate and original
control pass three real-memory cases. The existing nine API-double tests pass.
`focused-meson-tests.log` records all three registered test executables/modes
with zero failures (15 subtests, including repeated original-control cases).

A full unit-suite invocation failed while compiling the unchanged
`test-xbox-mcpx-apu-resampler.c`: missing `sinf`/`cosf` declarations are errors
under local GCC14. `full-unit-suite.log` preserves that failure. It is not a
passing full suite; the new CI results are still required.

The retained benchmark uses the same executable for original and candidate
modes, real RAM descriptors/payloads and a fixed number of blocks. Nine words
is one mono ADPCM encoded block (36 bytes); eighteen is stereo (72 bytes).
Warmup checks every word; each measured output contributes to an independently
validated checksum. Setup, warmup and checksum expectation are outside the
host monotonic-clock interval; cache creation/destruction are inside.

| Encoded words/block | Original µs per 1M blocks | Candidate µs per 1M blocks | Time saved µs | Improvement % |
| --- | ---: | ---: | ---: | ---: |
| 9 | 326,213 | 179,461 | 146,752 | 44.99% |
| 18 | 619,834 | 342,043 | 277,791 | 44.82% |

These are **single local observations**, without A/A, ABBA, BAAB or confidence
qualification. Both checksums match. They justify the bounded native experiment;
they do not establish a Deck or whole-game gain. Prior Deck game A/A controls
remain too variable for small-gain interpretation. No new Windows runs.

## Remaining gates

- Portable CI test/benchmark artifact and full-suite/build results.
- Maintained HTTP runner support for a non-QMP component process, then Deck
  `10.0.0.123` A/A and physical ABBA/BAAB. Current runner forces QMP.
- Concurrent memory/register activity, affected ADPCM native sound parity,
  and affected/control XISO correctness plus per-leaf timing evidence.

The identity check compares the current atomic map pointer to the view retained
by the cache. It never dereferences that current pointer; cache references
prevent the retained view's address from being freed/reused. A changed map
forces the original physical load. This is not synchronization across all
possible concurrent topology changes.
