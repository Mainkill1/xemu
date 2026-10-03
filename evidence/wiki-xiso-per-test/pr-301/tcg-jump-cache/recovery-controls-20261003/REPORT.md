# Recovery and compiler controls: prepared, native comparisons pending

Decision: HOLD. No new candidate speedup is measured in this packet. It preserves two built source controls and a fresh identical-parent A/A campaign. Deck operator authentication currently prevents installing the latest runner; HTTP remains reachable.

## Fresh A/A variation

Both labels use unchanged parent 76c23c7d, executable 5a3e3d8b..., runner55a360d, Deck .123, Vulkan/RADV, pinned original CPU leaves, explicit suite configuration. Physical ABBA then BAAB; median of four attempt mean guest work times with ten fixed iterations each. These apparent changes are variation, not optimization gains.

| Test | A median | B median | A minus B | Apparent change | ABBA | BAAB | Correctness |
|---|---:|---:|---:|---:|---:|---:|---|
| cpu_translation_blocks.code_stable | 2.532834 s | 2.529009 s | +0.003825 s | +0.15% | +0.14% | +5.28% | 8/8 pass |
| cpu_translation_blocks.code_rewrite | 3.133908 s | 3.011919 s | +0.121989 s | +3.89% | +1.32% | +4.08% | 8/8 pass |

All eight are correct, fully indexed and cold private Mesa qualified with no waiver/purge. All 2,480 artifacts collected with zero exclusions. Stable A4 is 2.8286295 s; every sample is retained. This order variation weakens interpretations of small future differences. A new runner revision requires a new A/A before comparisons.

## Built controls

- Inline-only source e4c8dfa6a91d7eb0ea9a4e479a4ad077dd7df193 changes only the parent tb_lookup forced-inlining attribute; no victim helpers/metadata. Builds pass before/after commit. Both parent and inline object lack an outlined lookup; register/code-shape differences remain the question.
- Bypass source ae939df5e36e989e4e4bd6a8b424a1ea26c90f7f retains enabled candidate callers, fills/history/invalidation and forces the out-of-line victim lookup to return NULL. Emulator builds and retained bypass test pass. New test first failed against real recovery; the original positive recovery case intentionally fails under bypass. Four applicable invalidation cases pass. This control removes search/promotion costs, not just successful recovery.
- Actual candidate versus bypass compiled caller .text/.text.unlikely/rodata bytes and actual text relocation entries are identical. Retained epoch/fill/invalidation helper instructions are identical. Only relocation section header ELF file offsets are normalized; raw headers and all entry offsets/types/symbols/addends are preserved. Entire ELF objects are not claimed byte identical.
- Full unit rebuild still fails in unchanged resampler missing math declarations; this is not a passing whole suite.
- Exact binary and companion hashes, source commits, frozen comparison orientation and verification scripts are in PACKAGES.json, CONTRACT.json and identical-caller-proof.json. Source branches are pushed for durable review; native inline/recovery comparisons have not started.

## Updated runner

Upstream1611918 (#99 screenshot completion after provider timeout) is incorporated at0e05af4 in existing runner draft89. All49 RunnerChecks pass including that regression. Deployment SSH key authentication was rejected; password environment unavailable. No remote test execution or authentication workaround. Deck remains55a360d; completed A/A evidence is applicable to that version only.

## Next comparisons

A parent / B inline-only isolates inlining. A bypass / B full candidate isolates lookup/search/promotion with identical caller instructions. Positive Improvement %=100*(A-B)/A. Complete a fresh A/A on the updated runner, then both physical orders; retain all failures and private cache qualification. VM/lifecycle, deliberate-collision guest and retail checks remain required before readiness. Useful XISO additions will receive their own source draft PR.

## Evidence

Public logs, all measurements and verification tooling are in EVIDENCE.zip. INDEX.json inventories every collected artifact and all packaged public files by local SHA256 and size. Private runtime state/cache bytes and duplicate diagnostic ZIPs are manifest-only; measured outcomes are included. No benchmark retry or sample was omitted.
