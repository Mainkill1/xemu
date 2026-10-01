# Issue #263: PVIDEO resource reuse

**HOLD: allocation-path improvement established locally; native overlay and
whole-emulator qualification remain open.** No Steam Deck or Windows game
performance result is claimed. The affected path is Vulkan PVIDEO overlays;
ordinary 3D draws may never exercise it.

## Change and lifetime

The reference is accepted main `ee5ce48b48784f999af374c1452003f8b2b1230f`.
`create_pvideo_image()` always created an image, view and sampler and never
recorded their dimensions. Repeated uploads normally destroyed and recreated
the resources. A synthetic matching-dimension/incomplete-resource case also
exposes its handle-overwrite branch; this is not a measured gameplay leak.

The candidate returns for a complete matching resource set, records dimensions
after successful creation, and resets dimensions/layout at destruction. Every
upload still converts and copies current pixels. `current_layout` selects the
existing production shader-read → transfer-write barrier on reuse and
UNDEFINED → transfer-write on fresh storage. The format argument now matches
the RGBA image. Existing auxiliary queue-idle waits remain, including the
display submission's completion before subsequent replacement. No new wait,
dirty-pixel shortcut, asynchronous retirement or guest timing change is added.

```text
Before: upload → destroy/create resource set → convert/copy → submit/wait
After:  upload → complete matching set?
                    yes: retain resource set
                    no:  destroy/create and record dimensions
                → convert/copy with tracked layout → same submit/wait
```

## Measured allocation-only result

The reusable [recipe](../../performance/issue263-resource-abba.py) executes the
real production creation path and real Vulkan/VMA calls. Each process warms
one create/destroy outside the timed batch, then performs 10,001 same-size
640×480 requests. Reference `display.c` is extracted from the exact commit
above and compiled with the shared current harness/struct headers/toolchain.
This isolates the resource function; it is **not** a pair of complete baseline
and candidate emulator runs. The reference does not use the added layout field.

Local host: Linux x86-64, GCC 14.2, O2/debug, no LTO; software Vulkan llvmpipe
(LLVM 19.1.7, 256 bits), driver version 109056002. The process and driver workers
inherit one fixed CPU affinity. Per-process XDG/Mesa directories start empty;
each contains 11 driver-generated files at exit, with paths/lengths/hashes
retained. The harness creates no application shaders; driver initialization is
outside timing. This is not the runner's native-game Mesa qualification.

`A` means reference; `B` means candidate. Four A/A reference runs precede ABBA
and BAAB. Reported cells are medians of two runs per variant/order, in
nanoseconds per resource request. Positive improvement means lower cost.

| Order | Reference ns/request | Candidate ns/request | Time saved ns/request | Improvement |
| --- | ---: | ---: | ---: | ---: |
| ABBA | 463.804 | 2.350 | 461.454 | 99.49% |
| BAAB | 381.562 | 2.350 | 379.212 | 99.38% |

Reference A/A range: **378.462–414.959 ns/request**. The order-dependent
reference variation remains visible; these short batches do not establish a
precise universal overhead. The absolute saving is only **0.38–0.46 µs per
request** here. A large percentage of this small helper is not a game FPS gain.
Both paths finish with one live allocation before teardown and zero afterward.
Creation requests fall from **10,001 to 1** for each image/view/sampler set.

All 12 v2 attempts exit zero. [Frozen v2 manifest](local-resource-balanced-v2/manifest.json)
binds source/ICD/executable hashes, flags, order, cache inventories and raw
stdout. [Summary](local-resource-balanced-v2/summary.json) and per-attempt logs
remain alongside it. v1 is retained: the recipe later gained an explicit
candidate rebuild, corrected working-directory metadata and complete cache
inventories; its measured binary/source fingerprints matched. v2 reruns the
corrected recipe and is the canonical table above.

## Correctness/build evidence

- Seven GPU-boundary tests include actual production `display.c` and `image.c`.
  They cover 10,001 same-size requests, both resize dimensions, incomplete
  resources, idempotent destruction/recreation, 1,000 resize/toggle cycles,
  changed source address/pixels with the same dimensions, and layout reset
  after upload/resize/destruction. GPU APIs are test doubles in this lane.
- Four original tests fail against unchanged production code, including
  **10,001 creations versus the expected 1**. The failure logs are retained;
  the two original resize/destruction controls pass.
- One opt-in real Vulkan test passes with actual VMA allocation/replacement/
  teardown on llvmpipe. It does **not** upload, sample, or submit GPU work.
- [Formatted focused tests](formatted-tests.log): seven boundary subtests and
  one real GPU lifecycle subtest pass through Meson's normal `--tap -k` entry.
- Reference and candidate emulator builds succeed. Existing warnings in
  unrelated `blit.c`/third-party code remain in compressed logs; no changed
  production function or new test emits a warning.
- **Full unit build blocked:** unchanged `test-xbox-mcpx-apu-resampler.c:74`
  lacks declarations for `sinf` and `cosf`; GCC 14 rejects it. The exact
  [full-unit build log](full-unit.log.gz) is retained. No compiler relaxation
  or unrelated header repair was included.
- Independent review caught test portability, standard argument parsing,
  allocator dispatch setup, layout-reset coverage and stale-build recipe
  problems. Those findings were corrected and verified with the current tests.

## Remaining gates

1. Run an overlay-heavy title/retained rendered fixture on Steam Deck with
   exact matched builds, A/A then ABBA/BAAB; measure creation counts, full
   upload/display CPU and GPU cost, resource usage and frame tails.
2. Validate changed pixels/source, resizing, disable/re-enable, reset/load,
   renderer restart and teardown with real rendered output and Vulkan
   validation, including delayed GPU use and scale/interlace/clipping/color key.
3. Collect affected XISO correctness/per-test timings and an unaffected control,
   separately for Vulkan and OpenGL. There is no XISO timing claim here.
4. Resolve or separately fix the baseline unit-build blocker; complete CI and
   required native correctness/resource gates before any readiness decision.

No Mesa waiver, global cache purge, extended endurance run, or merge occurred.
Evidence stays with the owning xemu implementation PR; the tests and recipe
remain reusable.
