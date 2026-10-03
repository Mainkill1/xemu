# ADPCM RAM lifetime qualification on Steam Deck

**Decision: HOLD.** This packet qualifies two retained component regressions in
[xemu draft PR275](https://github.com/Mainkill1/xemu/pull/275). Production code is
unchanged; this is correctness evidence, not a new speedup measurement.

| Check | Original reader | Candidate reader |
| --- | --- | --- |
| Local actual-memory fixture | 13/13 PASS | 13/13 PASS |
| Deck `.123` actual-memory fixture | 13/13 PASS | 13/13 PASS |
| Owned RAM retirement/replacement | 64 generations PASS | 64 generations PASS |
| RAM shrink exposing MMIO | 64 generations PASS | 64 generations PASS |
| Guard-deleted retirement control | PASS | Expected abort: stale `deadbeef` |
| Guard-deleted resize control | PASS | Expected abort: stale `deadbeef` |

Generation rows are subchecks inside the thirteen-case fixture. Each reader
observes 896 controlled changes: 768 existing mapping/register changes plus
64 retirements and 64 resizes. All 192 dynamically created QOM RAM owners
finalize before their counters leave scope. Shrink cases check every payload
and all eight MMIO access addresses/counts. Expectations are literal values,
independent of the cached-reader implementation.

## Exact builds and execution

Test source: `0024d1343d28340638f0f4455c16f42b5b3e6912`.
The README followup `072dd5b28b1` changes coverage prose only.
[CI37024778087](https://github.com/Mainkill1/xemu/actions/runs/37024778087) passes
all 20 jobs: ordinary units **137 pass / 16 skip / 0 fail**, the required Mesa
depth-draw check passes, and production Linux original/candidate fixtures each
pass thirteen subtests. The downloaded source archive matches the reader,
caller and test-source hashes. No test-source compiler warning appears in the
retained Linux release build output.

Native Deck runs use the same retained Clang/LLD release executable and all 38
libraries from its corresponding AppImage. Executable SHA-256:
`26ad98da1159c0a33c1e2cc09a14df59090da2ee71324612d205db344d2959cd`.
The maintained runner revision is `f5b3e58`; upload, execution and collection
use HTTP at `10.0.0.123:9368`. No Windows or SSH measurement is used.

| Mode | Run ID | Collection / outcome |
| --- | --- | --- |
| Candidate | `20261002-151402893-e91f5a2fb2dc4b479ce2cd5b3499d3d9` | 21/21 files, zero exclusions; completed/passed/complete/eligible |
| Original | `20261002-151407349-2c4f9003ebe14a04b7cb049696a3562e` | 21/21 files, zero exclusions; completed/passed/complete/eligible |

Both exit zero. Reusable saved tests remain installed:
`mcpx-adpcm-real-memory-candidate-correctness-v3` and
`mcpx-adpcm-real-memory-original-correctness-v3`; receipts pin immutable
revisions and all 39 build slots. The first save attempt exceeded the runner's
240-character description limit **after both measurements completed**. The
observer exception, rejected description and metadata-only recovery remain
preserved. No measurement was rerun.

## Ownership and ordering

The foreign reader registers with RCU and pauses in the second descriptor
callback. The BQL owner removes owned RAM, publishes replacement RAM, and
drops the fixture's last old-owner reference; alternatively it calls actual
`qemu_ram_resize` from 8192 to 4096 bytes. The latter exposes the lower-priority
MMIO region at the physical boundary even though allocation capacity remains.
The reader then resumes. It drains callbacks it queued after releasing its
cache; the main thread releases BQL during join, then drains final unmapping
callbacks. No sleeps establish correctness. Removing only the memory-view
identity clause gives a failing stale-word control for both cases.

These tests cover legal QOM-owned retirement and resize with explicit ordering.
They do not qualify arbitrary unsynchronized topology changes, same-map
backing-pointer replacement, destruction violating QEMU ownership, or completion
of every nested deferred physical RAM allocation free. Xbox system RAM is
normally fixed and machine-lived. Passing these tests does not qualify audible
output or whole-game behavior.

## Broader local failures retained

The first unfiltered local Meson invocation stopped before running because
`tests/qtest/qom-test` was not built. The first unit-only invocation similarly
lacked `test-crypto-block`. Building the unit executables hit the unchanged
GCC14 resampler test's missing `sinf`/`cosf` declarations. That compile failure
is preserved and explicitly excluded from the following local run:

| Local unit outcome | Count / detail |
| --- | --- |
| PASS | 135 |
| SKIP | 15 |
| FAIL | GL draw lifecycle SIGSEGV; filemonitor mutex assertion; char GCONTEXT assertion |
| TIMEOUT | AIO, 30 seconds |

All five failing/blocked test sources are unchanged from the pre-extension
revision; this comparison does not classify their causes. The local broad
suite **did not pass**. Focused final checks pass all three targets: nine
API-double cases and thirteen original/candidate actual-memory cases each.
CI passes on its separate environment; it does not erase local failures.

## Performance applicability and evidence

Production `vp.c` and `sample-memory.h` remain byte-identical to measured
`e25b0ba239c8146d2d661f26ec5ad29e82a14853`. Existing Deck component ABBA/BAAB
reported **+48.04% mono / +54.35% stereo** for fixed encoded-word read work;
[the original performance packet](../issue197-deck-component-20261002/README.md)
retains baseline/candidate times and controls. Whole-game
[ABBA/BAAB remains inconclusive](../issue197-game-balanced-20261002/REPORT.md).
No new performance or Mesa-cache qualification is claimed by these headless
memory tests. Audible parity, approved guest-fixture references and valid
XISO throughput contracts still keep PR275 draft/HOLD.

A fresh review found no Critical or Important source issue; its stale README
coverage note was corrected and its excluded scopes are stated above.
[`EVIDENCE.tar.gz`](EVIDENCE.tar.gz) includes full native collections, source,
mutation builds and expected failures, local failures, CI logs, saved-test
receipts and reusable HTTP orchestration. [`INDEX.json`](INDEX.json) hashes
every payload and records omitted large binaries retained locally/on the
runner. The archive was reopened and all payload hashes verified. No evidence
is committed to the runner or test-suite repository.
