# Issue #263: PVIDEO resource reuse

**HOLD: allocation-path improvement established locally; Deck rendered overlay
checks pass, but no useful guest throughput gain is demonstrated.** No Steam Deck or Windows game
performance result is claimed. The affected path is Vulkan PVIDEO overlays;
ordinary 3D draws may never exercise it.

## Change and lifetime

The reference is accepted main `ee5ce48b48784f999af374c1452003f8b2b1230f`.
`create_pvideo_image()` always created an image, view and sampler and never
recorded their dimensions. Repeated uploads normally destroyed and recreated
the resources. A synthetic matching-dimension/incomplete-resource case also
exposes its handle-overwrite branch; this is not a measured gameplay leak.

Candidate production source is commit `b0f0661860fc5c70a0be2bd2c20bb1bfc8c7e069`;
measurement manifests bind its file fingerprints. The pre-commit emulator
binary was used only to check compilation, not for any game comparison.
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

## Native Steam Deck validation

Both native full-emulator builds complete a 50-capture, 4x-work synthetic overlay
procedure. The retained host oracle maps guest 640×480 to the explicit
`107,0,1066,800` viewport in 1280×800 screenshots. It matches black/white
quadrants and RGB(51,76,153) background at 100 interior/exterior probes with
2/255 tolerance, and requires the ordered resize/off/on cycle. This checks
sampled colors and bounds plus a visual sequence; it does not prove every
pixel/frame, authenticate source-address phases or qualify delayed GPU use.
The source and background known answers are checked separately.

The [native report](native/README.md) retains exact binaries, fixture identities,
run IDs, source/hash oracles, raw failed attempts and full server comparisons.
The maintained test tools are [xemu-perf-tests draft #50](https://github.com/Mainkill1/xemu-perf-tests/pull/50);
emulator measurements remain here.

Four identical-parent controls precede ABBA and BAAB; `A` is the parent full
emulator, `B` the candidate full emulator. Each process runs both leaves, each
with eight 128-frame-submission batches, no warmup, multiplier 1 and
per-iteration completion. Cells below are medians of two per-run means per
build/order. Vblank pacing limits guest cadence; these are guest batch times,
not host display FPS. Improvement is `100*(before-after)/before`.

| Test / order | Backend | Before µs/batch | After µs/batch | Saved µs/batch | Improvement | Source correctness |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| steady_upload / ABBA | Vulkan | 2,133,067.688 | 2,132,867.188 | 200.500 | +0.0094% | PASS |
| steady_upload / BAAB | Vulkan | 2,132,599.750 | 2,133,001.625 | −401.875 | −0.0188% | PASS |
| resize_toggle / ABBA | Vulkan | 2,134,551.188 | 2,133,700.125 | 851.063 | +0.0399% | PASS |
| resize_toggle / BAAB | Vulkan | 2,132,485.313 | 2,132,518.563 | −33.250 | −0.0016% | PASS |

All 12 attempts complete with passing source/background oracles, complete
evidence and verified cold private Mesa disk writes. Reference A/A per-run
means span 2,131,634.625–2,133,453.250 µs for steady uploads and
2,132,628.625–2,133,581.250 µs for resize/toggle. The tiny sign-changing
differences do **not** establish a useful native throughput gain. The second
frozen campaign adds built-in host CPU analysis, described below.

## Native host CPU cost

The second frozen A/A → ABBA → BAAB uses the same complete emulators and guest
work. After a `frame=600 ` readiness marker, an authored 16-second segment
records host CPU through the runner's existing sampler and built-in analyzer.
This window is bounded by a guest frame marker and host duration; it is not the
exact guest batch boundary or isolated PVIDEO function cost.

Cells are medians of two per-run CPU means per build/order. **100 core % means
one fully occupied logical CPU**; savings in percentage points and relative
improvement are separate quantities.

| Order | Before core % | After core % | Saved percentage points | Improvement |
| --- | ---: | ---: | ---: | ---: |
| ABBA | 162.740328 | 162.143178 | 0.597151 | +0.3669% |
| BAAB | 162.652391 | 162.324859 | 0.327531 | +0.2014% |

All 12 second-campaign attempts pass. Each has 32/33 CPU samples, no missing
cells and zero collector overruns; collector duty averages 0.2832–0.3102%.
A/A controls span 162.874219–163.082281 core %; all eight reference means
including those controls span 162.433688–163.082281. The observed CPU reduction
is small relative to reference variation and differs between orders. Native
power policy is not fixed, and gameplay remains unmeasured; **a worthwhile
game speedup is not established**. The local resource reduction remains real.
Full server comparisons, both native campaigns' per-test timings/tails and
all failed diagnostics are in the [verified 3.07 MB packet](native/native-evidence.tar.gz).
No negative first-campaign row was discarded.

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
- [Formatted focused tests](formatted-tests.log.gz): seven boundary subtests and
  one real GPU lifecycle subtest pass through Meson's normal `--tap -k` entry.
- All **40 CI checks pass** at measured candidate head
  `aeeb9554a87f5e4cb706ff6c58b3825b16f320f1`, including CI unit builds.
  Reference and candidate local emulator builds succeed. Existing warnings in
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

1. Confirm whether the small native CPU saving justifies investment with
   representative overlay-heavy work. Whole-game/upload GPU cost, native
   creation/resource counts and resource growth/frame tails remain unmeasured.
2. Validate changed pixels/source, resizing, disable/re-enable, reset/load,
   renderer restart and teardown with real rendered output and Vulkan
   validation, including delayed GPU use and scale/interlace/clipping/color key.
3. Collect affected XISO correctness/per-test timings and an unaffected control,
   separately for Vulkan and OpenGL. Focused Vulkan guest batch timings are above; unaffected and OpenGL controls remain open.
4. Classify or separately fix the unchanged local unit-build blocker; complete
   required native correctness/resource gates before any readiness decision.

No Mesa waiver, global cache purge, extended endurance run, or merge occurred.
Evidence stays with the owning xemu implementation PR; the tests and recipe
remain reusable.
