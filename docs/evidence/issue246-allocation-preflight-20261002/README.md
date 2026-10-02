# Issue #246: texture allocation attribution preflight

**HOLD: the first allocation-churn gate remains incomplete.** This branch keeps
an offline reader and all three native attempts. It introduces no texture pool
or emulator runtime instrumentation. No performance improvement was measured.

This packet covers the original three attempts. The later
[Morrowind scene follow-up](../issue246-morrowind-scene-preflight-20261002/README.md)
retains Load v3 (No Saved Games) and New Game v4 (initial 3D ship interior behind
name-entry UI). The new captures do not replace the original failures.

## Baseline and candidate

**A** is exact fork main `ee5ce48b48784f999af374c1452003f8b2b1230f`, rechecked
through GitHub during this campaign. **B does not exist.** These are diagnostic
captures of A with existing `XEMU_VK_PERF_LOG` enabled and three 30-second CPU
profiles per attempt. They are not an ABBA/BAAB comparison or ordinary release
performance measurements. The physical order was PGR2, Morrowind v1, Morrowind
v2; the two Morrowind procedures differ as disclosed below.

| Main A capture | Observed log span (s) | Log frame records | Binding elapsed total (s) | Upload elapsed total (s) | Maximum binding duration accumulated in one log frame (ms) | Runner outcome |
|---|---:|---:|---:|---:|---:|---|
| PGR2 startup, Hong Kong loading, stationary grid | 166.962 | 3,784 | 3.946 | 2.686 | 27.675 | Completed / correctness passed / evidence complete / eligible for diagnostic contract |
| Morrowind v1: startup/title only | 107.125 | 6,050 | 3.506 | 2.685 | 4.202 | Completed / correctness incomplete / evidence invalid / ineligible |
| Morrowind v2: menu, New Game loading, dark text intro | 143.468 | 6,312 | 3.382 | 2.588 | 4.923 | Completed / correctness failed / evidence complete / ineligible |
| Candidate B | — | — | — | — | — | Not implemented; improvement percentage unavailable |

Binding and upload values are **host elapsed durations**, measured with
`g_get_monotonic_time()` in production source. Upload occurs inside binding;
**do not add these columns**. The maximum is the accumulated binding-region
work in one emitted guest-frame record, not a frame-time maximum, single
allocation latency or process CPU-time measurement. Log span is first-to-last
record timestamp, not full launch duration. Neither call counts nor upload
counts establish image-create counts or compatible eviction reuse.

## CPU attribution

Every profiler recording is matched to the native executable's ELF Build ID
and locally resolved against its exact debug file and libraries. The original
recordings, commands, build-ID lists, leaf exports, unfiltered children reports
and weighted summaries remain in the evidence packet.

| PGR2 A phase | Process userspace cycle samples | Source-visible observation |
|---|---:|---|
| Startup | 8,063 | `pgraph_convert_texture_data`: 1.557% of sampled cycle period; no named image-create/destroy leaf samples |
| Race loading | 9,392 | One `VmaBlockVector::CommitAllocationRequest` leaf sample, 0.011% of sampled cycle period |
| Stationary | 7,814 | No named VMA/image-create/destroy leaf samples |

The startup children report includes approximately 1.56% in texture upload and
1.76% in inlined `create_texture`. These are overlapping inclusive stack
attributions: `create_texture` includes hashing/upload/conversion and cache
hits as well as image creation. They cannot be called allocation cost or added
together. Absent named leaf samples do not prove zero cost: inlining, library
work and incomplete unwinding limit that inference. There is no measured bound
on a future optimization's gain.

The nine 30-second profiles all completed. Morrowind v1 captures title activity;
v2 reaches New Game loading and its text intro. Neither qualifies a Morrowind
open-world or texture-streaming workload. Its profiler results remain usable
for diagnosis with these limits, not for a passing performance comparison.
All 14 original screenshots were reviewed and are retained. PGR2 shows its
startup logo, Hong Kong loading, countdown/GO and a stationary red coupe at
0 MPH; it does not qualify repeated race/menu transitions.

## Failures and the maintained reader

Morrowind v1's complete 23,192,685-byte log exceeded the runner artifact
inspector's 16 MiB text/JSON limit during its `containsText` schema assertion.
That attempt stays invalid; its assessment was not edited or recalculated.
Its early Start input also preceded the visible Press START prompt.

V2 waits through the observed intro before Start, then chooses the highlighted
New item. It retains the original log existence/minimum-size requirement and
moves the schema assertion to a small, planned diagnostic report. The new
reader validates that the log's first record is schema 8 with unique named CPU
regions, using at most one 65,537-character read. This is the same schema
requirement checked without loading the whole live log. Its live report
explicitly says `liveHeaderOnly`, never claims complete-frame validation, and
records 23,510,626 source bytes at check time.

V2's final picture is a legitimate-looking dark intro with text, but the frozen
visibility assertion found only 1.445% of pixels above RGB 96, below its 5%
requirement. It therefore stays a correctness failure and ineligible. The
threshold was not lowered. It is not a renderer-regression finding or proof of
an active game scene. A future world capture needs an explicit, reviewed scene
procedure, not retries of this ambiguous intro boundary.

[`scripts/performance/vk-perf-summary.py`](../../../scripts/performance/vk-perf-summary.py)
is retained for reuse. Its default mode streams a finalized log, validates
frame ordering and nonnegative integer counter arrays, reports separate region
totals/maxima in milliseconds, and hashes the original bytes. It rejects
missing, truncated, oversized, unsupported or inconsistent records instead of
silently dropping them. Output files are created exclusively and never
replaced. It makes no allocation estimate, emulator call, clock change, cache
mutation or performance comparison.

```sh
python3 tests/unit/test-vk-perf-summary.py
python3 scripts/performance/vk-perf-summary.py vk-perf.jsonl --out new-summary.json
# Planned live diagnostic: validates the header only; full analysis is post-exit.
python3 scripts/performance/vk-perf-summary.py vk-perf.jsonl --schema-only --out new-header.json
```

Eleven checks pass, including a streamed log above 16 MiB, malformed/truncated
records, counter/order/schema errors and missing initial/interior frames, explicit nested units, a bounded live
header read and existing-output protection. The missing-reader red run, green
run and final tests are retained. Python compilation passed. Python 3.11 or
newer is needed for full-file hashing. Native v2 used the frozen reader in its
manifest. Independent review then found that offline mode accepted missing
initial/interior frame records. Both failing controls were reproduced, the
reader was corrected to require IDs 1, 2, 3, ... and all three original logs
were revalidated with unchanged totals. The live header/CLI functions used
on Deck remain AST-identical; the offline mode is stricter. Both source
identities and the review red/green logs are retained.

## Exact inputs and cache controls

Steam Deck **10.0.0.123**, runner
`0.2.0+848dca74e1ff79f9fc886769a785c23a0945a87e`, Linux x11, Vulkan,
full DSP with JIT enabled, VP workers 0, 128 MiB guest, surface scale 1,
vsync off, exact parent runtime library bundle. Native launches, diagnostics,
private-state materialization and collection used the maintained HTTP client.

- Native executable SHA-256: `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`.
- ELF Build ID: `7cc47cc0deb81956e198d71935d1a198116cdb02`.
- Matching debug SHA-256: `614ec7555af52d876e51de31263126b7fe700aac87c9592c218a959a24fffb0b`.
- PGR2 immutable disc: `research247-pgr2-disc-694365`; private HDD from `research247-pgr2-save-7b0f55`.
- Morrowind immutable GOTY disc: `issue236-morrowind-disc-d719c4d8`; private complete copy of `research245-morrowind-carrier-d62e336a`.

Morrowind cold-boots the copied disk; it does **not** restore the carrier's old
VM state or convert the disk snapshot into a different seed. All manifests
retain exact asset hashes. Private HDDs were deleted after confirmed exit and
evidence finalization; the original catalog assets remain intact.

All three attempts have complete, empty-before private disk-cache inventories,
`mesa_private_disk_writes_observed`, `verified: true`, no cache issues, no waiver
and cache `comparisonReady: true`. This does not override their separate scene
or evidence outcomes. OS page cache and driver memory cache remain explicitly
uncontrolled; these are not fully cold-machine or disk-I/O comparisons.
Each native stderr also retains a nonfatal GLib `g_source_destroy` refcount
assertion at shutdown. All three exited 0, but these are not assertion-free runs.

## Retained identities and reproduction

| Saved HTTP test/job | Archived native run |
|---|---|
| `issue246-deck-parent-pgr2-allocation-preflight-v1` | `20261002-032041568-ff04c9e06cf74cb08aafd646bb4af9e3` |
| `issue246-deck-parent-morrowind-allocation-preflight-v1` | `20261002-032832843-edc60ca1c66f4a4888bb63bcbdffc45d` |
| `issue246-deck-parent-morrowind-allocation-preflight-v2` | `20261002-033809674-62201412311a4bedb5a6a7959f32e00c` |

All three procedures were baked as immutable saved tests. Their revisions,
preparation recipes, manifests/configurations, submission receipts, canonical
assessments, cache reports and complete paginated collection receipts are in
[`records.tar.gz`](records.tar.gz). Use the maintained runner client's saved-test
selection with an explicitly identified application and a fresh attempt ID to
repeat a procedure. Preserve v1/v2's known limitations. Firmware, game images,
HDDs and emulator binaries are not redistributed in the packet; immutable
catalog IDs/hashes and exact release/build identities are retained.

[`manifest.json.gz`](manifest.json.gz) hashes included payloads and omitted
raw driver-cache files. Local symbol-resolution symlinks are omitted; build-ID
lists and resolution commands are retained. Every archive member was reread
and rehashed; see [`verification.json`](verification.json). Native measurements
live in this xemu branch, not the test runner repository.

## Remaining decision

The source audit confirms 1,024 LRU entries, separate dummy-image allocation,
current-bound/current-command-buffer eviction guards and synchronous submission
fence waits. Quarter-cache trimming is defined, but the budget query and its
pressure-triggered invocation are compiled out inside `#if 0`; an active
pressure drain is not established. Native BC normalization and
surface scaling can change the actual creation extent. Guest dimensions and
format alone are insufficient for compatibility. No external fork implementation
was copied or studied during this preflight.

PGR2's current diagnostic evidence does not establish material allocation
churn. Morrowind v4 now qualifies an initial 3D interior with modal UI, but
world/transition captures, another texture-heavy
title, actual create/destroy counts and duration, normalized configuration
repetition, eviction/in-flight reasons, allocation bytes and budget/pressure
behavior are still missing. Do not prototype a pool from this capture, claim a
speedup, mark the research ready or close #246 as a disproven optimization.
The next justified step is scene qualification and narrowly scoped lifecycle
attribution if its cost can be separated from conversion/upload, followed by a
bounded candidate only if the issue's first gate passes.
