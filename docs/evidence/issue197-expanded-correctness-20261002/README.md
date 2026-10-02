# Expanded sample-memory correctness

This packet adds local correctness evidence to draft [xemu PR275](https://github.com/Mainkill1/xemu/pull/275). The production reader and caller are byte-for-byte unchanged from measured revision `e25b0ba239c8146d2d661f26ec5ad29e82a14853`. The integration fixture changes; the older Deck performance packet remains bound to its original three-case executable. No new timing or native result is claimed here.

| Check | Original reader | Candidate reader |
| --- | --- | --- |
| Real QEMU memory fixture | 11/11 PASS | 11/11 PASS |
| Event-ordered RAM remapping | 256 generations PASS | 256 generations PASS |
| Event-ordered SGE table-base changes | 256 generations PASS | 256 generations PASS |
| Event-ordered SGE descriptor changes | 256 generations PASS | 256 generations PASS |
| Guard-deleted threaded RAM-remap control | PASS, exit 0 | Expected abort, exit -6; stale `deadbeef` vs `00010001` |

The 256-generation rows are subchecks inside each eleven-case fixture, not additional test counts. The separate API-double target passes nine cases; all three focused Meson targets pass with zero failures.

## Independent oracles and ordering

Four valid IMA blocks cover mono/stereo, within-page and SGE-crossing payloads. Encoded bytes are checked directly, then all 65 decoded samples per channel are compared with arithmetic ramps: left `1000 + sample`, right `-1000 - sample`. The MMIO payload callback checks every four-byte address and exact read count.

A foreign reader registers with RCU. QemuEvents pause it inside its second descriptor callback while the main BQL owner changes a retained RAM mapping, table base or descriptor page. Events establish ordering and the reader is joined each generation. Only the synthetic descriptor permits lockless MMIO, avoiding a BQL inversion. The table-base oracle distinguishes already-selected word 2 from newly translated word 3 with a distinct `feedbabe` sentinel.

Review found that word 2 initially matched both old and new banks in the table-base case. That minor oracle ambiguity was corrected before the final build and test run. No critical or important finding was reported. Earlier negative packages are retained with their exact source identities; `negative-reviewed` matches the final fixture. Its header removes exactly the memory-view identity eligibility clause, without editing production files.

## Evidence and limits

[`EVIDENCE.tar.gz`](EVIDENCE.tar.gz) retains fixture source/diff, complete focused Meson output, failed command invocation, earlier attempts, negative-control sources, compile/link commands, exit statuses and failure output. [`INDEX.json`](INDEX.json) hashes every archived file; the archive was reopened and all hashes checked. Large local shadow executables/objects are omitted with identities and remain retained locally. [`SUMMARY.json`](SUMMARY.json) records product source equivalence.

These are local Linux component tests. They do not qualify voice mixing, playback, native sound output, general decoder behavior, arbitrary concurrency schedules, or destruction/resize of RAM backing. Expanded Deck `.123` execution and new CI are pending. Existing component performance results are in [the separate Deck packet](../issue197-deck-component-20261002/README.md); whole-game performance and affected/control XISO measurements remain open. PR275 remains draft HOLD.
