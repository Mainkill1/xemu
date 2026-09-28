# All-shader capture: working validation

This is unfinished work on PR #259, based on published `06334cb5020c` plus uncommitted changes. It is not a merge or release approval. The complete source must still be committed, reviewed, built, and tested as one exact revision.

## Native preview gate

The production OpenGL and Vulkan preview lifecycle tests saved and reopened 32 independent draws using one pixel shader, with differing geometry, constants, and two texture inputs. Each backend completed all original/edited occurrence pairs. Later tests exercised captured destination seeding, blend/scissor/color-mask/cull state, source changes, and data-only seed changes. Measured fixture preview throughput was approximately 46–47 fps. This establishes worker/session behavior, not full real-title replay fidelity.

## Real Vulkan frame and archive

On the existing owned Windows test rig, Morrowind's restored test snapshot produced one captured guest frame with 1,151 emitted draw events and 26,037 ordered events. Recording completed, paused the guest, and retired all pending readbacks. Recorder-accounted CPU evidence was approximately 97.25 MiB.

The working Windows executable SHA-256 was `9035df01b824abc03f1e5fb7552e40ae53559f4966de491c63c0d88d7a8a4424`. This incremental diagnostic build contains the paged-archive and command-window corrections; it predates subsequent resource graph, inspection, raster, and timing changes.

The UI saved and reopened the same capture. The archive reports format 2, 26,037 events, 102 metadata pages totaling 41,727,826 bytes, and 12,403 deduplicated payload blocks. Archives and game resource bytes remain on the owned rig and are not published here.

Two concrete defects were discovered and corrected during this test:

- A single 32 MiB metadata file could not hold this real frame. Event metadata is now stored in bounded pages, with per-page size/count/digest validation and legacy format-1 read compatibility.
- The pre-dispatch command snapshot copied the entire remaining FIFO as lookahead. It now copies the active packet and only the seven words inspected by the draw-array coalescer.

The earlier harness forced close because `Process.MainWindowTitle` selected the separate workbench window. Explicitly closing that window changed the title back to the emulator; the subsequent harness close completed normally and verified the owned private disk. The original forced-close outcome remains recorded by the rig harness.

## Remaining native UI checks

Save/reopen passed. The occurrence table did not advance after injected arrow keys or Windows SendKeys; explicit stable-ID table navigation is being implemented and needs another native check. No real-game replacement/replay, rolling-window, full dependency replay, or OpenGL game-capture pass is claimed here.

## Remaining fidelity

The capture retains owned evidence and explicit limitations. Full ordered resource dependency replay is still incomplete. Current original-camera preview supports bounded compatible inputs, private RGBA8 color output, and explicitly substituted depth/stencil initial contents. Unsupported state is rejected. The UI must continue to distinguish observed checkpoints from replay output and synthetic diagnostics.

## Native Xbox fixture bring-up

An independently authored nxdk/pbkit fixture booted on the Windows Vulkan rig. The recorder retained one pixel shader with 32 draw occurrences, distinct six-vertex ranges, per-draw constants, alternating texture allocations, and its actual companion VS and host-generated GS. The first bring-up captured 344 ordered events, all readbacks retired, and approximately 4.61 MiB of evidence. This was the same incremental executable above, not the newest full working tree.

The first fixture setup omitted the texture border-source flag. xemu correctly interpreted the input as texture-stored borders (16×16 storage for a 4×4 logical image), while the fixture supplied only 64 bytes. The corrected fixture selects color borders and clamp-to-edge and renders the intended checker grid. This is a fixture correction, not an emulator behavior change. Corrected authored ISO SHA-256: `68c446c043f3254f79e9bd51e36771c9de7fb10326cc65707679f7008f6b50ad`; XBE SHA-256: `ed0e834602c603d3166602d6aa03dac17f24a8b28f381f04328d62e2c06fad20`.

Only native rendering and occurrence admission are established so far. The required corrected-scene archive/reopen/keyboard and original-versus-edited replay checks are still in progress. No hardware execution, exact full-frame replay, or all-use replacement validation is claimed by fixture bring-up alone.

## Complete integration build 6

A complete source overlay compiled the Windows emulator and focused session/resource/inspection/comparison/source-summary targets. The source manifest is held locally at `/tmp/pr259-integrated-build6-manifest.json`, with 129 audited source files, source tar SHA-256 `6431d6f85a049e181c8a229a95fb707690636527158442cb4e22ad284cae7de5`. Executable SHA-256: `7f7c10da63f2f6031b20e96858841ba91c65634034736a09da04fb77f474c341`. This is a working snapshot, not a committed release revision.

The native corrected fixture retained 32 matching draws / 346 ordered events / approximately 4.87 MiB with zero pending readbacks in this build. Clicking occurrences changes the bound textures/constants and event identities. Arrow-key navigation is still failing in rig tests, including correctly mapped Windows scan codes; no keyboard pass is claimed.

The build reported three new shadow warnings in comparison-test lambdas. The working source now uses distinct names, and runtime checks replace assertions so optimized `-DNDEBUG` builds still execute verification. Fresh strict debug and optimized comparison tests pass; another cross-build must verify warning cleanup on that source. Existing blit and WinPcap warnings are outside these changes.

Read-only integration review found allocation failures escaping C entry points, archive descriptor memory undercounting, and a split-lock stale-context cancellation race. Focused fixes and regressions are in progress. These findings further prevent a merge/readiness claim for build 6.

## Actual archived draw replay and depth clamp

The corrected authored fixture was captured, saved through the native Windows
workbench, and reopened on a separate Linux process. No live renderer handles or
current guest data supplied its replay inputs. Both recordings retain 32 uses of
one PS, distinct geometry and constant snapshots, two texture contents, and their
actual vertex and host geometry sources.

- Vulkan build-6 archive SHA-256:
  `14727152b70f14b368fff735ca0c54326128e652d3f39a23098942033fea294a`.
  346 events, 32 draws, approximately 4.87 MiB accounted input data.
- OpenGL build-6 archive SHA-256:
  `b810c5a8517092dce3fd4d1aaa33674ae7241c4284b9891b3b99b19c4fb1db12`.
  346 events, 32 draws, approximately 4.76 MiB, 411 shared payload blocks.

The actual Vulkan raster state enables depth clamp. The old preview rejected it
before executing any of the 32 draws. The working fix retains that state, enables
it only when supported by the private Vulkan device, and rejects an unsupported
device explicitly. OpenGL captures and applies its real depth-clamp state too.
Native lifecycle tests cover geometry outside both clip planes with clamp enabled
and disabled on both renderers.

With the fixed preview, fully optimized Vulkan archive validation completed 32
original and 32 edited replays. The first draw's 640×480 observed RGBA checkpoint
matched exactly: zero changed pixels and zero maximum/mean channel error. The
fixture disables depth/stencil testing; this does not validate unavailable original
depth/stencil destination data or arbitrary destination formats. All 64 replay
jobs yielded a measured whole-pipeline GPU interval.

The legacy OpenGL archive also completed all 64 jobs and yielded zero image error
at its first checkpoint. Its acceptance result is **failed**, because the old
recording omitted multisample-coverage and depth-clamp evidence. The pixel match
does not override that incomplete-state gate. The working source now records
single-sample coverage state; another native archive is required to validate it.

Commands and the detailed replay results are retained in
`actual-build6-vulkan-replay.log` and `actual-build6-opengl-replay.log`. These are
replay timings, not normal gameplay or stage-exclusive shader measurements.

## Actual live replay cadence

An independent temporary benchmark compiled all production preview code with
`-O2` and held the actual Vulkan occurrence's original stages, geometry, constants,
textures, raster state and owned before image unchanged. After a two-second warmup,
it measured an eight-second continuous window at 640×480:

| Measurement | Profiling enabled | Profiling disabled |
|---|---:|---:|
| Displayed/backend draw cadence | 46.25 FPS | 46.24 FPS |
| Preparation / GLSL compile / pipeline creation during window | 0 / 0 / 0 | 0 / 0 / 0 |
| Render worker wall time per draw | 1.261 ms | 1.197 ms |
| Render worker thread CPU per draw | 0.128 ms | 0.114 ms |
| Instrumented complete-draw GPU interval median / p95 | 0.101 / 0.136 ms | Not collected |

The first cold draw measured 65.373 ms, and alternating original/edited programs
across separate occurrence jobs produced similar cold intervals. The warm test
isolates those preparation changes from steady rendering. Driver lazy preparation
is an inference, not an isolated shader timing. The 46 FPS cadence is consistent
with the worker's 10 ms idle polling against the 16.67 ms paused scheduling interval.

This local measurement uses Lavapipe/llvmpipe, with a software HUD pumping much
faster than a normal display. It proves the captured live replay exceeds the
30 FPS target in this specific optimized test; it is not a native hardware/game
performance baseline. Benchmark instrumentation remains outside the repository.
See `actual-build6-vulkan-steady.log` for exact measurements.

## Native keyboard and editor workflow

Down, Home and End now passed native occurrence-table inspection, including
scrolling the last selected event into view. The earlier automated failure was
caused by the test helper omitting Windows' extended-key bit after MapVirtualKey
returned a plain scan code. Correct targeted window messages changed E97 to E105
without Enter, End selected E345, and Home returned to E97.

The source dock's Open occurrence preview/editor action still failed in build 6.
The capture action requested the In-game view tab late in the frame, but the tab
bar cleared that request before it could be consumed. The working fix consumes
requests before drawing other tabs. Another native build must validate the fix.

## Review corrections and file operation lifecycle

Focused optimized and sanitizer tests now cover C-bridge allocation failures,
internal recorder-incarnation guards for cancel/rearm, and conservative reopened
archive heap admission. Accounting includes retained descriptor capacities and
JSON preflight; it does not impose a hard process RSS or fragmentation bound.

Capture save/reopen/extraction now has a shared cancellable file-operation control.
Cancellation and final package publication use the same lock. Chunked file I/O
and phase counts provide progress without blocking the UI. Native UI verification
and session file-operation regressions are still being completed.

Resource queue-batch ownership, ordered downstream replay, complete destination
state, rolling/reset/budget native campaigns, exact-head CI, and the draft #241
dependency remain open. No build in this document is approved for merging.

## Additional native selection and cursor checks

The owned paused OpenGL workbench's OS cursor was sampled 400 times at each of
three fixed locations: image area, path field and GLSL source area. All 1,200
samples reported visible cursor state, with zero visibility transitions and zero
cursor-handle transitions at each location. This validates those stationary
workbench cases; it is not an exhaustive moving-pointer or multi-monitor check.
Results are retained in `native-build6-cursor-probe.json`.

A search with no matching captured events left E97, its image, resources and GLSL
active in build 6. The working selection fix clears the active event when the
filtered set becomes empty. Native verification is pending the complete build.
Retained-frame controls now filter inspection independently of the all-stage
recorder. Draw and event counts are displayed separately.

## Direct captured texture export

The input inspector now provides Export captured texture. Its asynchronous
operation exports every retained mip/face as PNG and exact host-decoded RGBA8,
plus shader/build/draw identity, guest/host formats and sampler metadata. It needs
no supported mesh and performs no late live texture lookup. Missing guest storage
and palettes are explicitly identified; this is not a recovered engine material
asset. File-operation progress and cancellation use the publication fence described
above. Focused optimized export regressions preserve bytes, mip/face identity,
sampler state, existing files, cancellation and invalid-image cleanup. Native UI
verification is pending the complete build.

Known unaccepted Vulkan command batches are now rejected from replacement
comparison, while failed completion produces an incomplete result. Retained
forensic events remain selectable. A regression covering pending, failed,
discarded, detached and accepted/completed outcomes passes in optimized builds.

## Integrated build 7: submission boundaries and native Windows units

Working snapshot, not a committed-head merge claim. Audited source-only overlay: 137 regular source/config/Meson files; SHA256 `813197c997e6b5faec9fca0370958d93e4272c6c03b9883c09f312d63ec4c8ee`. Emulator SHA256 `e2c34141afacc6691b452f6acb85c285528d57164d7315719495ce6dfb16d2ff`. Pinned Windows compiler image and compile-only builder remain unchanged. Build exited 0; new ReadEvent numeric-local shadow warning is assigned for correction before publication. No game execution on builder.

Native Windows rig ran ten focused executables, all exit 0: capture-session, capture-comparison, capture-resources, capture-session-resources, capture-inspection-ui, source-summary, capture-bridge-allocation, capture-file-control, capture-export, vk-capture-batch. The last links production Vulkan buffer/command functions and executes actual copy/queue/fence operations, including auxiliary-before-main, staging reuse, archive ownership and deterministic Stop-at-admission regressions. Native UI/game fixture acceptance is subsequent evidence, not established by these unit passes.

Independent submission review found a Stop/admission orphan token; failed admission now terminalizes all four affected non-emitted events. Temporary old-path native regression fails at Ready after Stop; current path passes. A separate archive receipt contradiction (Completed without accepted Submitted) is being hardened, and is not represented as validated in build 7.

Native authored OpenGL build7 captures frame6412, 346 events/32draws, all0 pending, PS3PF9-0BS9 / VS EKT8-Y1W4 / hostGSZ9YB-WW4Y. Saved and reopened owned recording at fixture-opengl-32-integrated7; event97 is draw1271373/emission628772. Reopened32 independent uses and original owned observed after image remain accessible. Native screenshot reproduces misleading successful-reopen message “Capture file operation failed.” (archive itself loaded; control already completed successfully). UI worker now sets success=true for reopen; strict UI compile exits0. Native message green is deferred to next updated build. Long inspector text clipping also reproduced; shared captured-input view now wraps available width, native visual green still pending.

## Native build 7 UI and replay investigation

Native OpenGL workbench validation passed immediate occurrence navigation, clearing the active inspector when a filter shows zero matches, and opening E97 directly into Source / replacement. Direct texture export produced `xemu.captured-texture.v1` with the selected event/build identity, sampler/format metadata, a 153-byte PNG, and exact 64-byte RGBA8 data (SHA256 `5d8c813ce87489c64e4baeb6ab6a4170f12330fa588bc65f81fd7db381ceaf2a`). This is the authored 4×4 fixture texture; no commercial title resource was transferred. Successful-reopen status and paragraph clipping corrections remain subsequent to this executable.

The authored OpenGL recording ZIP SHA256 is `1bbb2d438c8b4797a3f3a5a1c371faeba087ec16e230bf39e7ab3dec8a407bdc`. Its 32 original plus 32 edited uses pass on Mesa, including the independently retained checkpoint (maximum RGBA8 channel error zero; coverage mask 0x3FF). Native NVIDIA OpenGL 4.3 driver 581.95 intermittently produced repeated occurrence images or identical original/edited images. All 64 jobs completed with keyed timings, but this fails acceptance. A consumer detach/rebind experiment still failed, and is not applied to production.

Extra producer readback diagnostics, and later worker readbacks queued only after consumer copying, passed and reported matching worker/consumer checksums. Twelve delayed-audit runs passed, but those extra GL operations perturb execution and do not establish a fix. Diagnostic sources remain outside the repository; builder overlays must be restored from an audited production snapshot before publication. Native Vulkan control using the authored build6 archive and build7 lifecycle executable passed all 64 jobs and the observed checkpoint, with 32/32 original/edited measured intervals (median/p95 0.041984/0.047104 and 0.040960/0.045056 ms). NVIDIA OpenGL corruption remains a concrete merge blocker under investigation.

## Owned comparison images and dependency execution

Comparison image acquisition now queues a bounded readback on the producing preview worker. Exact generation leases prevent retirement during the download and terminal timing collection. Returned pixels/timing are immutable CPU data and may be used after GPU texture retirement. Normal live preview does not request payload download. The comparison UI consumes the owned timing cache directly; its old live-lease timing preflight could miss already completed results. FrozenInputs reruns now use the existing globally unique comparison row token for both packet view revisions, so previous images and timings cannot satisfy a fresh run of the same capture/edit; compilation identity stays unchanged. That rerun regression demonstrated RED before the change and optimized strict GREEN afterward.

The exact native authored OpenGL recording passed 64 original/edited jobs in three independent NVIDIA runs using the production owned-readback path, with distinct images, visible source effects and unchanged observed checkpoint (tolerance one RGBA8 unit; actual error zero). This establishes that comparison path; it does not independently explain every driver behavior in the earlier cross-context texture readback or prove ordinary live presentation has no remaining driver issue.

A fresh native Vulkan recording retained frame9191, 444 events/32 draws, all inputs retired, accepted batch and completion receipts. Owned archive ZIP SHA256 `299afb2557aae2b13c1808be62426db7bfed265c88d1b0a05f0ec49f6e521a7b`, uncompressed bytes5603602. E102 identifies draw1482252/emission731415; PS3PF9-0BS9, VSJTGK-ZXQD, hostGSZ9YB-WW4Y. Saved/reopened recording passed all64 native original/edited jobs and its checkpoint. Replay GPU medians/p95: 0.041984/0.063488 and 0.042496/0.063488 ms; diagnostic replay numbers, not game-performance baselines.

The new native dependency test uses synthetic owned records with full supported raster/stream/source inputs and a different downstream pixel shader. Actual worker draw output flows through a canonical CPU image copy into the consumer. On both Mesa OpenGL and lavapipe Vulkan, original producer and consumer are red; editing only the producer makes both green, and the retained old blue texture is unchanged. Genuine build7 comparison source fails that test on both backends with zero jobs/Unsupported; current complete lifecycle tests pass. The copy is reconstructed CPU logic, without a GPU-copy timing claim. Timing adapter tests separately validate real native Vulkan image copy capture descriptors. This is not yet end-to-end title dependency replay or full-frame fidelity.

Integrated snapshot8 source audit:145 regular source/config/Meson files, archive SHA256 `393fc7df8e6df5dec20ea043729e430bbc5e4319d6bc95aa53c2b31bf7f43e46`. Includes all production files rather than diagnostic source overlays, receipt validation, actual Vulkan image-copy descriptors, source-exact draft eligibility/offline Save, successful-reopen status/wrapping/export-parent fixes, ordered replay, native helper and owned result transfers. Build and native acceptance are recorded separately after completion. New replay-unit local shadow warning at line448 is assigned for routine rename; pre-existing WinPcap and unmodified blit warnings are distinguished. Explicit post-trigger memory/event reservation and supported color-clear replay are subsequent work, outside this frozen snapshot.


## Integrated snapshot 8: build, native archive, and blend precision

Windows compile-only build exited 0 (104 targets), producing emulator SHA256 `c36932cb9318a22f0308828da02ca774b37b0ffcc05864d118226bfd65ad3401` from the frozen snapshot8 source manifest above. Thirteen native unit executables completed before the lifecycle gate: session, comparison, resources, session-resources, replay, inspection UI, source summary, bridge allocation, file cancellation, export, Vulkan capture batch, draft and apply. Test logs are retained under the owned rig integrated8-tests directory.

The actual authored build7 OpenGL and Vulkan archives each passed native snapshot8 original/edited comparisons for all32 captured draws (64 draw jobs), with one independently observed checkpoint supported and checked. GL median/p95 associated draw intervals were64000/74752ns original and65536/73728ns edited; Vulkan41984/63488ns original and43008/62464ns edited. These are instrumented replay draw intervals, not exclusive shader timings or gameplay performance.

The full native lifecycle initially failed both backends at an exact UNORM8 blended pixel assertion. A source-only diagnostic returned original `[147,50,50,255]` / edited `[20,50,177,255]`, whereas software Mesa returned `[148,50,50,255]` / `[20,50,178,255]`. The test now allows one unit only in blended red/blue channels; preserved green/alpha, scissor-excluded pixels, repeated results and dependency test endpoint colors remain exact. This changes the test, not the renderer. Relevant precision rules: [OpenGL4.3 core specification, Blending](https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf), [Vulkan framebuffer blending](https://docs.vulkan.org/spec/latest/chapters/framebuffer.html). Diagnostic builder overlay was restored to the production test immediately afterward.

Fresh strict local lifecycle builds and executions passed Mesa OpenGL and lavapipe Vulkan after the correction, including the producer/copy/consumer test and32 reopened occurrence comparisons.640x480 captured-mesh displayed cadence was46.5fps GL and46.0fps VK in that local test. Native Windows lifecycle verification of the corrected test and integrated UI acceptance are recorded separately; no merge/release readiness follows from the archive passes alone.

Native Windows precision-corrected full lifecycle passed both renderers (exit0), including actual worker/HUD transitions, original camera inputs, depth clamp, owned-before raster tests, producer/copy/consumer propagation and32 saved/reopened native worker occurrences.640x480 displayed cadence was32.0fps OpenGL and31.5fps Vulkan in this harness. Replay median/p95 associated draw intervals: GL57344/60416ns original and57344/59392ns edited; VK43008/45056ns for both. The corrected test executable is separate from the frozen8 emulator binary; native UI testing uses the unmodified frozen8 emulator hash above.


## 2026-09-28 — catalog drag crash regression

The previous payload tag `XEMU_SHADER_BROWSER_SHADER_KEY_V1` contains 33 characters. ImGui's payload type permits at most 32; invoking the real `SetDragDropPayload` aborted with its exact size assertion (exit 134). The catalog and replacement target now share `XEMU_SHADER_KEY_V1`, with a compile-time check against `ImGuiPayload::DataType` storage.

The existing selection UI test drives mouse press, drag, target hover and release through actual ImGui. It checks the shader identity is copied, does not change when the local source is modified, and is delivered only on release. Existing keyboard selector cases remain exercised. Strict local compilation and run passed. The same Windows test passed on the native validation rig (exit 0). Source and target production translation units passed strict syntax compilation, and the Windows xemu target linked successfully.

This focused native build overlays only the shared tag/header and selection UI test on integrated snapshot 8. Executable SHA256: `cb394cb1c67d0ce9d4d3dbdd2c6c891b88b93b22a434e1d930874a26751b43b4`. It is an unpublished working validation artifact, not a merge-ready PR head. Full running catalog interaction remains a separate gate.

Local logs: `/tmp/pr259-drag-payload-red.log`, `/tmp/pr259-selection-drag-red.log`, `/tmp/pr259-drag-stage3-compile.log`, `/tmp/pr259-dragfix8-native-build.log`. Native unit log: `C:\xemu-test-runner\pr259\drag-selection-native.log`.


## 2026-09-28 — PGR2 Vulkan baseline and integrated snapshot 9

Baseline native automatic search reproduced the user's failure: screenshot retained 494309 matches skipped for Unsupported topology. A bounded forensic capture preserved 300 emitted draw occurrences independently, with 3016 ordered events, no pending payloads on termination, and an explicit incomplete/budget outcome (42.63 MiB retained after transient reservations retired). Metadata inspection found 1 triangle-list draw, 296 triangle-strip draws, and 3 quad draws. Baseline diagnostic geometry was empty on strips. This is actual PGR2 evidence, not a synthetic inference. The original PGR2 HDD seed SHA256 `8b4f7c81be6ece5db3fc98d683d86d0515db43b2447e8dff14bfd9b45078c606` remained unchanged after normal close; the game ran only on a verified private clone. Commercial resource bytes remain on the validation rig.

Integrated source snapshot 9 SHA256: `a39aa98bfd786fac2ef9c23fc9164c32a86d719e5b2511ee7555cce5ea4d9062` (149 source/config/test files). Windows build completed 141 targets with exit 0. Executable SHA256: `54c346d93756d1db43b3904a96dc5d431e5bd2dea501dd6018b82c4a50bbb87a`. This is an unpublished working snapshot, not an exact published PR head.

Fresh local GL/Vulkan production-worker lifecycle suites passed, including all 32 independent original/edited occurrences, producer->copy->consumer propagation, scalar readback correctness, and profiled shutdown/restart. On the native Windows rig, both full production-worker lifecycle suites passed (exit 0), including GL's 20 new filled-topology branch image comparisons. The native steady 640x480 display measurements were:

| Backend | Paused | Running normal pressure | Running high pressure |
|---|---:|---:|---:|
| OpenGL | 60.0 fps | 60.5 fps | 30.5 fps |
| Vulkan | 32.0 fps | 32.5 fps | 30.5 fps |

These are short-window measured test-scene display rates, not universal title throughput or stage-exclusive GPU cost. Preview scheduling preserves deadline phase and uses a bounded 1 ms worker wait; missed intervals produce one newest result rather than a backlog. Normal/High interval policy remains 60/30 Hz. Strict release and sanitizer service/clock tests include regular and jittered polls, freeze/recovery, output ownership and cancellation.

Native integration identified two CPU test fixtures that manually created a legacy triangle-list pipeline without the new explicit host enum. Their validators correctly rejected the unknown topology; the fixtures are being updated without weakening production validation. Snapshot 9 is not claimed as an all-tests pass until these tests are rebuilt and rerun.


## 2026-09-28 — native PGR2 strip capture and wide-target replay

The snapshot9 real PGR2 Vulkan search found QNNV-PMXF at guest frame16468,
draw21578386/emission10760740 and paused automatically. Extracted metadata
confirms guest primitive6 (triangle strip),65 original indices,44 copied
positions and189 diagnostic triangle indices. The event owns VS/PS/host-GS
source,2/2 texture slots,28 uniform records and178 state fields; observed
before/after targets are1280x480. The catalog mouse drag and actual replacement
target drop both completed in the live emulator without a crash. Held payload
shows the correct title/shader identity. With no replacement selected, delivery
reports the missing-package condition and does not enable a rule. Native
selection UI regression separately checks owned payload bytes and release-only
delivery.

Creating a replacement exposed a concrete additional blocker: the old native
preview640x480 limit refused PGR2's actual1280x480 viewport. Captured pipeline
limits now permit1920x1080 with16 MiB owned pipeline data, within the unchanged
32 MiB packet cap; synthetic remains640x480. Native pipeline ownership is attached
before packet validation. A review allocation-fault reproduction proved
oversized pipeline data must be rejected before digest serialization; the new
preflight passes that check. Offset/nonintegral/negative-height viewports,
primitive restart, unsupported formats and missing dependencies remain explicit
limitations rather than silently rescaled replay.

Local strict/model/sanitizer and comparison suites passed after meaningful RED
for the1280x480 target. Full local OpenGL and Vulkan production-worker suites
passed including1280x480 rendering, preserved owned destination pixels and exact
owned readback dimensions. Standalone Vulkan also exercised1920x1080,
cached-pipeline reuse and pre-allocation bounds rejection. Native Windows runs
of the corrected comparison/replay CPU fixtures pass; the previously failing
standalone Vulkan color test confirmed127,127,0,255 for nominal0.5,0.5,0,1.
Only fractional R/G channels now permit one conversion unit; endpoints, source,
topology and indexed/array image equality remain exact. This is test precision,
not a production rendering change.

Reviewed snapshot11 source archive SHA256
`3858266f35785fa1a1416c4467e1f22955b3e1b3c1ed2c9b8e89bb1ed87cd905`;
Windows emulator SHA256
`864e2b378e050e717a3a90791136dfc49a0bc51042a6815bc900f22715a3e297`.
Build exited0. The emulator is unpublished; live PGR2 replay/replacement and
exact published-head CI remain separate validation gates. Source10/11 supersede
the earlier test fixture overlays without rewriting their original manifests.

PR260's design-only Asset Browser is additive. The implementation plan documents
shared occurrence/resource versions and scoped shader identities, with asset
assembly and GLB extraction kept separate from this recorder and replacement
library.

### Snapshot11 native execution and replacement activation gate

All18 native CPU/UI units passed. Standalone Vulkan passed three consecutive
runs. Full native OpenGL and Vulkan lifecycle suites passed, including the
1280x480 original-camera target, owned destination readback,32 saved/reopened
occurrences, producer/copy/consumer propagation, and last-good output lifecycle.
OpenGL measured60.0/60.5/30.5fps at640x480 in paused/normal/high-pressure cases;
Vulkan32.0/32.0/31.0fps. These are bounded authored-scene measurements.

Actual PGR2 Vulkan snapshot11 captured a strip using QNNV-PMXF at frame8947,
draw8328426/emission4152695. Creating the source copy compiled and rendered at
1280x480, explicitly labeled partial replay. A comment-only edited draft saved
to the custom launch-profile replacement library without enabling a rule.
Explicit Enable then revealed a separate runtime blocker: renderer stateFailed,
effective actionNormal, error `Vulkan fragment interface is incompatible`.
The saved source has6703bytes/236lines and retains its original uniform block.
The existing Vulkan replacement validator rejects every nonempty uniform block,
including normal generated pixel source. Native preview success therefore does
not establish successful runtime activation. This is an open gate pending a
safe generated-interface compatibility correction.

One-click Disable removed the rule and the next supported strip captured and
paused successfully at frame9857/draw11043811/emission5505102. Actual observed
before/after output and2/2 texture slots remained inspectable. Commercial shader
and resource payloads stay on the rig; this record contains metadata only.

### Authored filled-topology scene: native capture, save and reopen

The32-use variant XBE ran on both native renderers with snapshot11. The Vulkan
recording contains444 events and OpenGL346; each retains32 emitted draws, no
pending payloads, and the expected per-mode counts: list6, strip6, fan5, quads5,
quad-strip5, polygon5. Each shader filter reports32 uses, with six distinct host
geometry variants rather than inventing a single companion GS. Both archives
were saved and reopened through the actual workspace; Down selected the next
occurrence immediately. Normal close verified unchanged original HDD seeds.

Archives remain under the owned rig as
`fixture-topology-vulkan32-integrated11` and
`fixture-topology-opengl32-integrated11`. Authored ISO SHA256
`e62a48130eab87c6d5f3e912863b8b9675c75df03da65c3c69d6a06948981442`;
XBE SHA256 `f8502466d4ffed4496b074b73688d8d78b89acef158f1eb6ef1a2de93cae1d4b`.
The original native archive acceptance test rejected event135 because it assumed
all32 draws had an identical GS. The mixed-topology fixture legitimately uses a
GS per primitive mode. The updated gate requires shared VS/PS, stable GS within
each topology, and the exact original fixture or mixed-fixture topology counts.
Native original/edited replay of these new archives is recorded separately.

## 2026-09-28 — user-requested stability checkpoint

The user requested a reviewable stopping point before returning to Steam Deck
performance work. Broader recorder/replay/asset features are parked at this
checkpoint; PR259 remains draft and stacked on241.

### Vulkan generated-source replacement correction

The replacement validator now checks the existing `PshUniformInfo` ABI instead
of rejecting all uniforms. It accepts unique known names with exact32-bit scalar
types/signedness, vector/matrix shapes, array counts, flattened upload dimensions,
offsets/strides and bounded nonoverlapping ranges. Safe subsets/reordering use
the existing name-based writer. Unknown/custom uniforms, push constants, uber
blocks, descriptor arrays and incompatible bindings still fall back safely.

Pinned SPIRV-Reflect reports zero matrix stride for an array of matrices. The
validator proves actual unique `OpMemberDecorate` MatrixStride16 and ColMajor
for the exact struct/member, with bounded instruction traversal. A changed
literal8 fails even with unchanged cached reflection. The six real generator,
compiler, reflection and `uniform_copy` cases pass strict optimized and
ASan/UBSan builds (leak detection disabled). Independent read-only review found
no remaining critical/important issue. The Windows-native six-case unit also
passes; it does not replace the following actual-game check.

Snapshot12 source SHA256
`07616416f9323ebf71047ff3e52cbcacc40cb9341c2f44496fefa33af1b73a2c`;
emulator SHA256
`1ee818aa262bc2b924592d04cf47a20dda2a632723d51f9db884111546710ea0`.
Actual PGR2 Vulkan QNNV-PMXF strip capture paused at frame6666,
draw5150879/emission2568515. A comment-only edited draft r20 compiled against
the1280x480 captured target, saved inactive to its custom-profile library, then
reported **Game: Effective** after explicit Enable and matching game draws.
One-click Disable returned **No override**; normal captured output resumed and
paused at frame9859/draw9470417/emission4721220 with2/2 textures retained.
Normal close verified the original HDD seed unchanged. These results close the
snapshot11 activation blocker for this tested shader; they do not prove every
shader interface, persistent-rule restart or full replay fidelity.

### Final formatting and build identity

Snapshot13 differs from12 only by required formatting in four new capture-input
headers. All new source files pass clang-format checks, and diff whitespace is
clean. Its151-file source archive SHA256 is
`0eacc635e83b69eb6c7491589d7e6a911b661cbcda7d75fe1d660244d60ca006`;
the final Windows emulator SHA256 is
`22f1c40e29ed635398cd9015c4042d1b8e5347b2f7d22b6627ac761c556a8e2e`.
The compile-only pinned build and affected native targets exit0. Native actual
runtime activation above used snapshot12; final worker/archive acceptance is
recorded below. No commercial capture bytes or generated game shader source are
published in this evidence.

Remaining gates include full dependency/depth/stencil fidelity, broader native
title coverage, rolling/reset/budget/overhead acceptance, persistent-rule restart,
Steam Deck validation, actual published-head CI and the draft241 dependency.
The usable-draw action's old search message may remain after the new capture is
Ready; the capture identity and paused state are correct, but that status copy
needs a follow-up. No merge readiness or pre-release readiness is claimed.

Final snapshot13 native archive acceptance exits0 on both renderers:64 completed
original/edited jobs per backend, all32 measured on each branch, and1/1 supported
independently observed checkpoint reproduced. OpenGL original median/p95
51200/56320ns and edited50688/63488ns; Vulkan34816/38912ns and34816/40960ns.
These are instrumented whole-draw replay intervals, not exclusive shader cost or
normal gameplay timing. The final formatted emulator is installed hash-pinned
on the rig; no owned game process remains running. General lifecycle/FPS evidence
is the separately documented snapshot11 run, rather than inferred from this
archive-only acceptance path.

### Published checkpoint CI version failure (2026-09-28)

Runs36412542073 and36412548561 on85e4959c9423 failed before compiling the
shader changes. The source job selected the ancestor preview tag as
`0.0.0-pr259-preview.06334cb502-3-ga67b373dbf`; every failed platform/unit job
then rejected that archive's version metadata. Cancelled matrix jobs provide no
build result. This is separate from the pinned local/native validation above.

The version generator now excludes preview tags when locating a numeric base
tag. Source packaging uses that same validated generator, including the checked-in
base version for tagless forks. Regression cases cover an unrelated preview tag
with only file metadata and a newer preview tag masking a valid numeric tag.
The added test failed before the fix with the exact CI error, then passed.
`bash tests/unit/test-xemu-version.sh "$PWD/scripts/xemu-version.sh"`, shell
syntax checks and `git diff --check` pass. An independently executed actual
workflow version step with a preview-tagged temporary repository produced
`0.8.136-0-g<source commit>`; the resulting gitless archive metadata was accepted
by the build generator with the same version and source commit. Full CI on the
follow-up commit remains a separate gate.

### Vulkan report-fixture link closure (2026-09-28)

The271c71953c platform builds passed, but both full unit jobs stopped at linking
`test-xbox-pgraph-vk-reports`: report retirement now references capture APIs,
readback allocation helpers and the S3TC decoder absent from the report-only
fixture. No unit-test execution pass was implied by those successful builds.
Compiling the current report/command/draw production sources against existing
local QEMU generated headers and utility libraries reproduced the same missing
symbols before the fixture correction.

The report fixture now models a disarmed capture service using the real API
declarations. Empty batch lifecycle calls require zero handles; payload staging,
GPU readback and timing publication fail immediately if unexpectedly reached.
The link includes the production S3TC decoder. No application behavior changes
are involved. Local strict compilation and report integration execution pass all
eight cases, including command-buffer completion before DMA publication and
disabled telemetry preserving behavior. The intentional registration shim still
produces the existing unused `nv2a_register` warning, explicitly exempted from the
local strict gate; the modified fixture code has no warnings. Version regressions
and whitespace checks pass. Full exact-head unit CI remains required after push.

### Profiling test registration follow-up (2026-09-28)

The later86de7ad6e0 push and PR CI runs completed successfully,20/20 jobs each;
the push unit suite reported181 passed,17 skipped,0 failed. During the PR242
supersession audit, `test-xbox-vk-shader-timing` was found among the skipped
targets: its silent exit-code driver was registered by the common TAP runner.
The assertions were present, but no executed cases were represented in TAP.

The existing timestamp-wrap, invalid-bit-count, pipeline-variant and query-admission
checks now use the project's GLib test registration and comparison assertions.
A local check of the previous driver failed because four TAP cases were absent;
strict optimized compilation of the corrected driver produces four passing cases.
Formatting and whitespace checks pass. No renderer or profiling behavior changes.
Current-head CI must confirm the test is reported as executed rather than skipped.
