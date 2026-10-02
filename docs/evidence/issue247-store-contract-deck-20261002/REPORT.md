# Voice-store memory contract qualification on Steam Deck

**Decision: HOLD / draft [PR276](https://github.com/Mainkill1/xemu/pull/276).**
The existing equal-store candidate has useful matched Deck VP measurements.
This additional qualification closes specific mapping and dirty-client test
coverage gaps. It adds a retained integration fixture, without changing
production behavior or the measured voice-frame fixture.

## Results at a glance

| Check | Result | Meaning |
| --- | --- | --- |
| Local focused tests | 25/25 cases across five executables | 3 policy, 1 earlier memory, 6 VP, 6 diagnostic VP, 9 new memory cases |
| Deck release integration process | 9/9 cases, exit 0 | Current-source production memory path, all declared output checks |
| Final-source negative controls | 5/5 defective executions rejected | Complete-word validation, owner checks, MMIO writes, and changed stores matter |
| Ordered writer accesses | 20,000 | 10,000 before validation and 10,000 after completion, with event handshakes |
| Owner lifecycle | 73 finalizations | Includes 64 sequential legal retirements; no live APU retirement claim |
| Full local unit build | Failed | Unchanged resampler test lacks sinf/cosf declarations under GCC14 |
| New performance measurements | N/A | Correctness-only test addition; test duration is not a timing metric |

The separate [baseline/candidate ABBA/BAAB report](../issue247-deck-voice-frame-20261002/REPORT.md)
retains fixed-production VP improvements of **2.64–4.08%** and all variation,
counts and failures. A/B mean original physical stores versus the existing
RAM equal-store candidate. Source equivalence below binds those prior
measurements; no game FPS or new speedup is inferred from these nine cases.

## What the retained tests exercise

[`test-voice-store-integration.c`](../../../tests/xbox/mcpx-apu/test-voice-store-integration.c)
links actual production archive objects and wraps only the executable entry.
QEMU allocates 2 MiB RAM with QOM owners. The fixture uses the real system
address space and enables NV2A, NV2A_TEX and NV2A_SURFACE dirty clients.
Expected words are literals, rather than values computed with the helper.

| TAP case | Independent assertion |
| --- | --- |
| changed-masks | Eight byte/full/register-mask cases give literal words; every changed store dirties all three clients. |
| equal-clients | Equal partial/full words remain unchanged and all three clients stay clean. |
| fallback-guards | Equal low-address and unaligned stores reach dirty observers. |
| alias-remap | After the read, a new alias resolves another page; CAS mismatch falls back to that page. |
| other-ram-owner | Equal contents in another RAM owner still take physical fallback. |
| mmio-remap | A remap preserves one equal and one changed MMIO write, with expected values. |
| ram-resize | Legal shrink exposes MMIO; regrowth uses current RAM without an old extent. |
| ordered-writers | Only unmasked fields change; full-word validation preserves existing stale-read RMW fallback, while a writer after completion wins. |
| owner-retirement | Each of 64 owners finalizes after legal removal/unref/RCU drain; the next owner receives its expected word. |

Mapping changes occur under BQL. QemuEvent handshakes order ordinary C RAM
accesses; the correctness mechanism uses no sleeps. Alias/MMIO regions remain
alive through required cleanup. Finalization counters do not assert that
every subsequently queued RAMBlock reclamation callback has finished.
The [usage and contract document](../../../tests/xbox/mcpx-apu/VOICE-STORE-CONTRACT.md)
retains the command line and exact limits. Meson and Linux CI build, run and
retain the target, including the existing aarch64 matrix entries.

## Negative controls and failures

The retained mutation tool compiles separate helper objects and links them
with actual production objects. It does not modify production contents or
replace the normal helper object. Five final-source executions reject:

| Defective helper | Rejecting case | Observed failure |
| --- | --- | --- |
| Return on original read equality | ordered-writers | 0xaabbcc44 survives instead of original RMW result 0x11223344 |
| Return on original read equality | mmio-remap | Equal MMIO write count is zero instead of one |
| Validate only selected bits | ordered-writers | Unmasked writer change wrongly admits return |
| Admit any RAM owner | other-ram-owner | Other-owner dirty observer remains clean |
| Drop changed physical store | changed-masks | Required changed literal word is absent |

Five earlier pre-format negative executions are also retained. The final
pass was made to bind the controls to an exact fixture/helper hash; it adds
source snapshots and leaves the earlier artifacts intact. Both phases fail
the intended assertions, not startup. No native attempt is rerun.

Retained local setup failures: missing explicit address-space/RAM declarations
on the initial compile; a signed-value oracle assertion caused by ldl_le_p's
int return type (fixed with uint32_t casts); invoking meson absent from PATH
(fixed by the existing build pyvenv path); a verification command with the
wrong working directory (corrected to absolute paths before checks); and the
unchanged full-unit math header failure. Patch-check failures and correction logs are preserved: block
comment style, signed-off patch invocation, and Markdown license parsing were
corrected. Final-tree checkpatch still flags the __wrap_main prototype for the linker-wrapped
entry point in C and asks about MAINTAINERS. The declaration follows the
existing fixture convention and satisfies compiler missing-prototype checks;
this is recorded, not described as a clean checkpatch pass. clang-format and
git diff whitespace checks pass.

One early canonical-body update stopped before PATCH because GitHub still
reported the previous PR head. The subsequent update checked the expected
head and exact body readback. The superseded e0a8823 CI runs were already
cancelled when an explicit cancellation was attempted; their actual states
are retained. Neither cancellation is a test result. Two CLI job-log fetches returned empty
files; build qualification refused them before native launch. The direct
GitHub job-log API supplies the verified compiler and nine-case result.

## Source, build, and native identity

- Contract source: `abcb752ca4211922511eb76de32b6b9d2e803daa`.
- Successful source CI: [37046677970](https://github.com/Mainkill1/xemu/actions/runs/37046677970).
- Compiler/linker: Clang/LLD 21.1.8, release ThinLTO, x86 version 3.
- CI source archive matches the fixture, helper, documentation and registration.
- Executable SHA-256: `bb5c37ebe1a581bda62006333ef2399556b76704cab8f3a5e9ce56a6798985d7`.
- All 38 runtime libraries match the earlier measured VP campaign.
- All **39 executable/library slots** are saved before execution.
- Native host: **Steam Deck 10.0.0.123**, HTTP 9368, maintained runner f5b3e58.
- Job: `issue247-store-contract-correctness-20261002`; run: `20261002-183109816-d52ff144677741a39c3bf7e8ef1718b2`.
- One native attempt: completed/passed/complete/eligible outcome; full collection
  with zero exclusions, empty stderr and zero exit code.

The correctness-only indexed record intentionally has no metric and cannot
support a timing comparison. Its performance-index exclusion is not a test
failure. The archive retains the full paginated record, pinned request,
manifest, launch/results/output and collection receipts. The first index GET returned build_results_not_found after full collection;
a later read found the same completed run. The retained tool now bounds that
index-availability wait. Resumption rereads and collects the existing job;
it never launches a second attempt. Windows and SSH perform no native
measurements in this qualification.

The entire hardware tree and existing ordinary/diagnostic VP fixture are
byte-identical to measured candidate 5d832c02c3ed6e1f56ef1b808fe74d8f3ea565ee.
The retained source-equivalence check applies the older performance evidence
to this test-only delta, without mixing #197's reader patch into the branch.

## Remaining gates

Unsynchronized guest/vCPU stores are not exercised here. RCU protects mapping
lifetime; it does not serialize all RAM writers. The fork's atomic/memory/TCG
source remains authoritative, with upstream [atomic rules](https://www.qemu.org/docs/master/devel/atomics.html)
and [memory ownership rules](https://www.qemu.org/docs/master/devel/memory.html)
providing context. Event-ordered tests cannot substitute for that concurrency
contract.

Live APU reset/save-load, sample ordering, affected audible audio, TCG code
observers, Xen/migration/debugger observers, and qualified affected XISO
references/timings remain open. No renderer consumes the tested dirty bitmaps.
Further game comparisons still require controlled Mesa cache and confirmed
start-input consumption; this headless fixture does not repair those gates.
Keep PR276 draft/HOLD.

[Complete packet](EVIDENCE.tar.gz), [every payload and large-binary omission](INDEX.json),
[summary](SUMMARY.json). Archive SHA-256: `08778777eadb39e2c249e3e8ff540e549a91a3f94fe0b27c3f04b4a761e40a5a`.
