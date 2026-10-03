## Recommendation: NEEDS TESTING

**Triage:** The bounded ADPCM RAM-mapping idea remains plausible, but the Deck differences were mixed and all comparisons ineligible; the XISO control did not exercise ADPCM. Use an affected audio workload, byte-level parity and controlled A/B before deciding whether to retain it.

**Summary:** This branch changes the non-streaming MCPX APU ADPCM block reader to reuse a bounded RAM mapping for contiguous physical sample words. Exact-head CI and local boundary tests pass. Eight visually matched Steam Deck PGR2 runs and a four-attempt Windows XISO control provide no qualified performance gain. Do not merge on the present evidence.
**Remaining:** Run byte-level ADPCM parity and native audio checks, an APU-exercising workload with controlled cache state, and a pinned-oracle XISO comparison.

### Problem

The non-streaming ADPCM fetch in `voice_get_samples()` resolves the SGE descriptor and performs a QEMU physical-memory lookup for each of nine or 18 encoded words. Issue #197 identifies repeated physical lookup as a possible VP-worker cost. The issue's older profile is motivation, not a measurement of this branch.

### What this PR changes

- `hw/xbox/mcpx/apu/vp/vp.c`: `read_adpcm_block()` groups only the current encoded block by SGE page, creates a short-lived `MemoryRegionCache` for each multiword chunk, and uses cached little-endian word loads when the returned RAM span covers the word.
- It re-reads the SGE descriptor before every later word. A changed descriptor, short map, non-RAM range, or word crossing the mapped end takes the original `ldl_le_phys()` path. Every initialized cache is destroyed before the next chunk.
- `sample-memory.h` and its unit test cover page chunking, short spans, changed physical mappings, and overflow-safe cache eligibility.
- Streaming ADPCM, PCM sample widths, SGE translation caching, voice scheduling, notifications, and shader code are unchanged.

### Why this approach

Before:
```text
each encoded word -> read current SGE -> translate physical address -> load word
```
After:
```text
one SGE-page chunk -> map bounded RAM span
each word -> read current SGE -> same physical span and full width?
    yes -> cached little-endian load
    no  -> original physical load
destroy mapping before next chunk
```

The current per-word descriptor observation is retained, so this slice does not depend on the unresolved same-table mutation contract of PR #190. The cache lifetime is limited to one encoded block chunk; it never extends past the SGE page, returned RAM span, or block. This trades one map setup per chunk for fewer physical translations on contiguous RAM.

### Validation

- Parent: `ee5ce48b48784f999af374c1452003f8b2b1230f`; current candidate: `3768494853`. Commit `3768494853` changes formatting only on top of `a810c8d500`.
- Local Linux x86-64: `ninja -C build -j4 qemu-system-i386 tests/unit/test-xbox-mcpx-apu-sample-memory` passed; the modified VP C file compiled without warnings. `build/tests/unit/test-xbox-mcpx-apu-sample-memory` passed 2/2; `git diff --check` passed.
- Exact-current-head CI run [36843817991](https://github.com/Mainkill1/xemu/actions/runs/36843817991) passed, including Linux, Windows, macOS and unit-test jobs. Native game audio, byte-for-byte sample parity, and exact-symbol attribution remain unverified.
- Affected XISO: audio/sample-memory behavior requires an APU exercising fixture; neither currently registered tester suite exposes an ADPCM leaf. After Windows C: cleanup resolved the 1 GiB free-space preflight, the `cpu_floating_point.sse_scalar` unaffected control ran A/B/B/A. All four guest leaves report PASS with the same framebuffer hash, but the runner marks correctness failed because this suite has no pinned oracle and its cache state is uncontrolled. Its guest per-test timing is below.

### Results

The first Deck PGR2 procedure started recording on a loading screen despite passing its generic image checks, so that run is excluded. A separate diagnostic procedure keeps the same menu inputs and adds a 35-second settle before the 30-second measured segment. Start and end images for all eight runs show the same car at 0 MPH at the starting line. The order was A/B/B/A then B/A/A/B, where A is the exact parent and B this PR.

| Deck parked PGR2 | CPU mean (core %) | Guest cadence (fps) | Frame mean / p95 / p99 / max (ms) |
| --- | ---: | ---: | ---: |
| A1 | 314.02 | 19.00 | 50.97 / 68.14 / 98.59 / 120.12 |
| B1 | 314.38 | 18.84 | 51.23 / 69.79 / 90.59 / 122.36 |
| B2 | 314.57 | 18.86 | 51.16 / 68.41 / 91.77 / 134.94 |
| A2 | 314.21 | 18.85 | 51.12 / 66.88 / 89.64 / 155.19 |
| B3 | 312.81 | 18.40 | 52.47 / 70.10 / 99.18 / 156.15 |
| A3 | 313.59 | 16.26 | 57.37 / 81.02 / 105.99 / 141.77 |
| A4 | 313.48 | 18.42 | 52.56 / 69.59 / 89.11 / 187.82 |
| B4 | 314.74 | 18.61 | 51.76 / 70.34 / 90.31 / 162.91 |

The one slow A3 run drives an apparent average cadence advantage for B; group median cadence is 18.64 fps for A and 18.73 fps for B, while mean CPU is 313.82% for A and 314.13% for B. The differences are mixed and small relative to run variation. All eight comparisons are **ineligible** under the tester's uncontrolled Mesa driver-cache state. These observations do not establish a gain or a regression.

Windows XISO control, 10 guest iterations per attempt, unchanged framebuffer hash `6962077c244da325`:

| Attempt | Guest leaf | Mean / p95 (ms) | Total guest test time (s) |
| --- | --- | ---: | ---: |
| A1 | PASS | 515.56 / 546.31 | 5.156 |
| B1 | PASS | 520.34 / 567.41 | 5.203 |
| B2 | PASS | 548.34 / 556.66 | 5.483 |
| A2 | PASS | 539.45 / 559.12 | 5.394 |

These are guest-reported per-test times extracted from the virtual HDD; the runner's overall attempts are not comparison-eligible and this CPU leaf does not exercise the changed ADPCM path.

### Risks and remaining questions

- The mapping setup may cost more than the physical lookups it removes, especially for small blocks, fragmented mappings, or non-RAM inputs.
- Per-word descriptor rereads preserve SGE mutation visibility, but byte-level equivalence under concurrent guest writes, page transitions, and map invalidation still needs native qualification.
- One game workload cannot establish PCM coverage or universal APU benefit; PCM is out of scope here.
- An APU-exercising XISO leaf with a pinned oracle and controlled driver-cache conditions is outstanding, so this is not merge-ready.
- The original Windows XISO campaign failed preflight due to disk space; a new four-attempt campaign executed after cleanup. Its guest leaves pass, but runner-level correctness and comparison eligibility remain failed for the stated oracle and state reasons.
- The Deck's Mesa driver-cache namespace is not isolated by the current tester. The balanced PGR2 observations are diagnostic only.

### Related work

Addresses #197. PR #190 separately explores descriptor translation caching; this branch does not require it. PR #272 owns TCG lookup attribution, and PR #270 owns voice-write attribution.

> **Agent Declaration**: This pull request was created with assistance from Codex / GPT-6.
