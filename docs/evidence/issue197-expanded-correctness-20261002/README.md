# Expanded sample-memory correctness

This packet adds local and Steam Deck `.123` correctness evidence to draft [xemu PR275](https://github.com/Mainkill1/xemu/pull/275). Expanded fixture revision: `c4cdef6cad3dd22d516e16ec2cb1aa9c06ec11f6`. The production reader and caller are byte-for-byte unchanged from measured revision `e25b0ba239c8146d2d661f26ec5ad29e82a14853`. The integration fixture changes; the older Deck performance packet remains bound to its original three-case executable. No new timing result is claimed here.

| Check | Original reader | Candidate reader |
| --- | --- | --- |
| Real QEMU memory fixture | 11/11 PASS | 11/11 PASS |
| Native Steam Deck real-memory fixture | 11/11 PASS | 11/11 PASS |
| Event-ordered RAM remapping | 256 generations PASS | 256 generations PASS |
| Event-ordered SGE table-base changes | 256 generations PASS | 256 generations PASS |
| Event-ordered SGE descriptor changes | 256 generations PASS | 256 generations PASS |
| Guard-deleted threaded RAM-remap control | PASS, exit 0 | Expected abort, exit -6; stale `deadbeef` vs `00010001` |

The 256-generation rows are subchecks inside each eleven-case fixture, not additional test counts. The separate API-double target passes nine cases; all three focused Meson targets pass with zero failures.

[CI36999719003](https://github.com/Mainkill1/xemu/actions/runs/36999719003) passes all 20 jobs, including original/candidate real-memory modes in Linux production builds. Its downloaded source archive exactly matches the committed reader, caller and expanded fixture.

## Native Deck controls

Both runs used the same unmodified retained x86 release executable and 38 corresponding AppImage libraries, uploaded/launched/collected through the maintained HTTP process workflow. Executable SHA-256: `d4ae22cbd5922d3ec81b37b525740c9e503321fc01e26915083497c0d2f53f70`. All 39 payload files are pinned in the archived manifest. Both runs exited zero, reported eleven TAP successes, passed correctness/evidence checks, and completed collection with no excluded files. These correctness-only runs have no performance metric.

| Mode | Run ID | Result |
| --- | --- | --- |
| Candidate | `20261002-111828346-2886c20d7c3841aca85223a7451c21d0` | 11/11 PASS |
| Original | `20261002-111832179-b292b462acad429aa803de41dd9ef731` | 11/11 PASS |

Reusable immutable saved tests remain installed as `mcpx-adpcm-real-memory-candidate-correctness-v2` (revision `3c688c7a31da04ab86e24cb6eb8be0b4e49878b2417c6cdf394ed88c90f6b8cd`) and `mcpx-adpcm-real-memory-original-correctness-v2` (revision `c31f4fce1d5de9a4d9cf68b0c29711912af259c0c0a5743489ce89812a72ea49`). Each declares all 39 build slots for future executable/library replacement. The integration target remains in normal Linux production CI. No Windows box run or renderer/Mesa activity occurred.

## Independent oracles and ordering

Four valid IMA blocks cover mono/stereo, within-page and SGE-crossing payloads. Encoded bytes are checked directly, then all 65 decoded samples per channel are compared with arithmetic ramps: left `1000 + sample`, right `-1000 - sample`. The MMIO payload callback checks every four-byte address and exact read count.

A foreign reader registers with RCU. QemuEvents pause it inside its second descriptor callback while the main BQL owner changes a retained RAM mapping, table base or descriptor page. Events establish ordering and the reader is joined each generation. Only the synthetic descriptor permits lockless MMIO, avoiding a BQL inversion. The table-base oracle distinguishes already-selected word 2 from newly translated word 3 with a distinct `feedbabe` sentinel.

Review found that word 2 initially matched both old and new banks in the table-base case. That minor oracle ambiguity was corrected before the final build and test run. No critical or important finding was reported. Earlier negative packages are retained with their exact source identities; `negative-reviewed` matches the final fixture. Its header removes exactly the memory-view identity eligibility clause, without editing production files.

## Evidence and limits

[`EVIDENCE.tar.gz`](EVIDENCE.tar.gz) retains fixture source/diff, complete focused Meson output, failed command invocation, earlier attempts, negative-control sources, compile/link commands, exit statuses and failure output. It also includes the complete original Deck collections, payload manifest, saved-test receipts, orchestration source and successful CI record/raw REST logs. The first CLI log download returned an empty file; it remains preserved beside the complete REST download. [`INDEX.json`](INDEX.json) hashes every archived file; the archive was reopened and all hashes checked. Large local shadow executables/objects and the native package binaries are omitted with identities and remain retained locally/server-side. [`SUMMARY.json`](SUMMARY.json) records product source equivalence.

These are local Linux and native Deck component tests. They do not qualify voice mixing, playback, native sound output, general decoder behavior, arbitrary concurrency schedules, or destruction/resize of RAM backing. Existing component performance results are in [the separate Deck packet](../issue197-deck-component-20261002/README.md); whole-game performance and affected/control XISO measurements remain open. PR275 remains draft HOLD.
