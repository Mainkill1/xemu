# Issue 197: per-word sample-memory parity repair

## Decision: HOLD

The prior candidate captured `NV_PAPU_VPSGEADDR` once for an encoded block.
Main reads the table-base register before each word. Re-reading a descriptor
through the captured base did not preserve a table-base change.

The reader now takes the register address and reads its value for each
translation. It is in a private inline header so the unit test invokes the
production algorithm directly. PCM and streaming ADPCM paths are unchanged.

| Check | Before correction | After correction |
| --- | --- | --- |
| Table base changes after mapping word zero | FAIL: second word `0xdeadbeef`, expected `0x55667788` | PASS: later words come from new table |
| Descriptor address above 4 GiB | Extraction initially FAIL: read address zero | PASS: retains original `hwaddr` arithmetic |
| Focused reader suite | Original PR had 2 helper tests | 9 tests PASS |
| Modified production VP compilation | N/A | PASS with `-Werror` |
| Corrected candidate native performance | Not measured | Not measured |

Tests cover table-base and same-table descriptor changes, noncontiguous SGE
pages, unaligned words across an SGE page, short/failed/non-RAM mappings,
wide descriptor addition, and nine/eighteen-word little-endian block data.
The physical-memory/cache APIs are deterministic doubles using real QEMU
public types. Cache resource checks cover the double's lifetime, **not**
QEMU address-space replacement or concurrent remapping. These checks do not
qualify native audio or a performance gain.

`red-extraction.patch` and `red-sample-memory.h` preserve the behavior-neutral
extraction used to reproduce the original base-capture bug. The patch changes
the reader interface to a register pointer but deliberately captures its value
once, so the old behavior remains. `red.txt` records its semantic failure.
`red-wide-address.txt` records the review-discovered extraction error before
its correction; `pre-width-fix-suite.txt` retains the failed intermediate suite.
`green-final.txt` records the corrected nine-test run. Initial compilation
with additional `-Wextra` triggered pre-existing QEMU-header warnings; the
retained log shows them. Final warning flags match the project's `-Wall`
configuration and exclude its existing negative-shift warning.

The focused build uses current source headers and generated headers/external
dependency includes from the existing local issue246 build based on
`ee5ce48b48784f999af374c1452003f8b2b1230f`. The scripts and exact commands are
retained. This is **not a matched native performance build**.

## Prior measurements and next gates

`prior-pr-body.md` preserves the full previously published eight-run Deck
ABBA/BAAB and four-run Windows CPU-control tables and their failures. All were
ineligible, and no speedup was established. They do not test this repair.
The earlier CPU control did not exercise ADPCM; its missing pinned oracle and
uncontrolled cache state are retained. No Windows run was added here.

The newer collector-OFF Deck profile in [draft PR293](https://github.com/Mainkill1/xemu/pull/293)
attributes 9.31% of sampled process userspace cycles to physical longword
reads on identified voice-worker threads. It does not distinguish descriptor
reads from sample payload or ADPCM from PCM. It motivates measuring this
candidate, without predicting its benefit.

Native work is restricted to Steam Deck `10.0.0.123`. Remaining gates:
matched builds, affected ADPCM/native audio parity with a pinned oracle,
controlled Mesa cache state, A/A then physical ABBA and BAAB timing, and
concurrent QEMU mapping-lifetime coverage. Keep PR275 draft until these are
complete. No merge is authorized.
