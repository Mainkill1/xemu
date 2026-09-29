# Live car stage replay implementation plan

> **For agentic workers:** Use superpowers:executing-plans, implement and verify each task, then request a fresh whole-change review.

**Goal:** Show a coherent PGR2 car assembly, including animated wheels and captured shading, from a stable inspection camera during gameplay.

**Architecture:** Extend the existing owned occurrence recorder and Asset Browser. Explicit live asset acquisition retains draw-input snapshots rather than claiming a complete forensic event/dependency stream. Verified generated position-transform patterns provide placement relative to the selected anchor; original captured stages and resources render the ordered selected parts into one shared depth/color target. Preserve the existing raw-input diagnostic view and forensic recorder.

**Tech stack:** Existing C++17/ImGui/GL HUD, Vulkan capture owner, immutable blocks, generated NV2A GLSL, native Windows HTTP tester.

**Spec:** `../specs/2026-09-28-pgr2-live-assets.md` and the user's latest fixed-angle, connected-car/wheel/shading acceptance test. This completes the spec's outstanding original-stage/live-pose path; the previously completed diagnostic viewport is not sufficient.

## Constraints

- Existing PR260 and capture ownership; no new PR or competing shader compiler/library.
- Exact draw generations and owned resources; no delayed reread of mutable guest bytes.
- Generic placement/membership suggestions remain inferred; same-model opponents must remain ambiguous if evidence cannot distinguish them.
- One coherent frame for every displayed assembly; fixed camera and bounds must not reset on each update.
- At least30 FPS viewer target, and independently measured pose update/capture overhead.
- Forensic capture always preserves its ordered events; explicit lightweight live input acquisition must report its missing dependency closure.
- No synthetic material substitution in captured-stage mode. Missing required data produces a visible incomplete status.
- Preserve texture precision, shader interfaces and host numeric attribute interpretation.
- Keep captures, program/mesh/texture caches and pending jobs bounded; shutdown while HUD context is valid.

## Review focus

1. Reused shader/model data and identical cars do not prove player identity.
2. A wheel transform must place it within the anchor's coordinate system, not independently center it.
3. Sampled Y16 depth retains16-bit precision and actual component mapping.
4. Camera override updates host depth/clip bookkeeping without claiming game-camera output equivalence.
5. Stale or incomplete input generations cannot silently reuse the old Lab sphere or mix part poses.

## Tasks

### 1. Own the sampled Y16 texture used by the real car

Files: Vulkan `shader-browser-inputs.h`, shared draw-input/model helpers, native adapter and material regressions.

- [x] Pin the current rejection with a native adapter test: guest linear Y16 + host R16_UNORM is a color sample representation, not an unsupported depth attachment read.
- [x] Retain an exact raw storage blob alongside the existing decoded inspection image, recording format/dimensions/component mapping.
- [x] Make existing replay upload the typed storage at full precision, and validate malformed/absent bytes without fixture substitution.
- [x] Run strict adapter/product compilation and native precision/material regression.

### 2. Establish captured placement and coherent related parts

Files: new bounded asset placement helper; model/controller; CPU regressions.

- [x] Verify the generated direct position-transform pattern against the retained PGR2 body draw; unsupported patterns remain unsupported.
- [x] Extract a finite invertible clip-from-local matrix from owned constants and captured viewport state, with raw-bit preservation in evidence.
- [x] Transform candidate bounds into anchor space; identify related part candidates with stage/target/spatial evidence and explicit confirmation.
- [x] Follow confirmed membership using stage/layout/material and placement evidence, rejecting duplicate/ambiguous matches and partial poses.
- [x] Test moving/rotating wheels, duplicate cars, draw reorder, changed LOD, singular/nonfinite transforms and coherent frame publication.

### 3. Render captured stages in one assembly target

Files: asset viewport/replay adapter and generated-source camera helper; native GL fixtures.

- [x] Adapt owned host GLSL/uniform layouts to the HUD inspection backend using the existing generated source and captured interface metadata.
- [x] Cache programs, typed streams/textures and samplers; execute original VS/material stages for all selected occurrences in emission order with shared depth/blend state.
- [x] Apply one anchor-relative camera after the original vertex processing; retain game-provided material inputs. Keep camera/bounds fixed through live updates.
- [x] Handle generated geometry/depth bookkeeping and clip controls explicitly; failed replay hides unrelated outputs.
- [x] Test two rotating wheels/body/glass, distinct materials, depth occlusion, transparent overlap, full precision depth samples, camera continuity and caller GL state.

### 4. Update only relevant live draw inputs

Files: capture settings/admission/persistence, asset live/controller/UI; ownership regressions.

- [x] Add explicit live-draw-input acquisition to the shared recorder; default all-shader forensic admission is unchanged.
- [x] Initial discovery acquires bounded draws and complete latched inputs without recording thousands of redundant state-write events.
- [x] Confirmed assembly follow narrows acquisition to supported stage/layout candidates; publish only complete same-frame poses and coalesce stale jobs.
- [x] Expose captured-stage/raw-input display, related parts, fixed camera, freshness, failures and freeze controls clearly.
- [x] Test independent forensic capture, reset/close/re-arm, unsupported parts, budgets and stale worker completion.

### 5. Native end-to-end gate

- [x] Strict changed-product TUs, focused CPU/native tests, sanitizer gates and formatting.
- [x] Fresh review, resolve important findings with meaningful regressions, publish existing branch and obtain actual-head CI (63f7e934; subsequent changes require new CI).
- [ ] On the Windows rig, select the identified player car, confirm its connected parts, drive and steer while viewing it at a constant angle; observe wheel motion and actual captured color/shading.
- [ ] Preserve images/video or ordered frame evidence with exact build/settings/source identities, measure viewer cadence and pose freshness separately, and qualify freeze/reset/duplicate cars.
- [ ] Remain draft if native fidelity, performance or stacked dependency gates are incomplete.

## Execution ledger

- Start: existing isolated worktree `xemu-pr260`, clean implementation b4c566.
- Evidence: native occurrenceE38773, PS1RYS-QZYP, body positionsVK_FORMAT_R16G16B16_SNORM, requiredT1 guest0x30/hostR16_UNORM rejected. Original vertex source retained privately; position usesc114..117 followed byc58/c59 viewport conversion. This is evidence for a guarded transform adapter, not a general Xbox matrix convention.
- Ruling: extend the existing approved original-stage/live-pose specification and implement inline under the user's explicit real-car test direction; no repeated implementation authorization request.

- Task1 verified local strict material/model, owned native adapter, adjacent-value Vulkan GPU comparison and actual-flags Vulkan product TUs. Native PGR2 resource capture/replay remains an end-to-end gate.

## Native follow-up gates (2026-09-29 UTC)

The exact-head Windows context correction passed CI20/20 on both triggers.
Native run2 rendered all37 confirmed car parts and followed driving/steering,
but remained incomplete: HUD31–38FPS during live capture, pose updates2.1–2.4/s,
underside default angle, and pre-display colors. A frozen pose renders above
180FPS; this is not a matched gameplay performance measurement.

1. Retain the exact DAC palette at draw capture, synchronize its owned copy
   with palette writes, and map the shared color target once after blending.
   Keep legacy missing palette status explicit; reject mixed/malformed data.
   Runtime palette regression: blended rawR102 must map to ownedR201; mapping
   parts before blending would yieldR52.
2. Use an above-body, closer default inspection angle and reset it consistently.
3. Requalify exact-head CI and native captured paint after this correction.
4. Profile capture/readback/decode before redesigning continuous pose acquisition.
   The current next-frame request/pending-retirement/rearm lifecycle does not
   establish30 fresh poses/s. Do not report HUD cadence as pose cadence.

Both native failed attempts remain archived without acceptance markers.

### Native follow-up: repeated texture acquisition

The saved exact3a native live frame has61events,1050texture images but55unique
immutable image blocks, and128363064bytes of repeated decoded texture evidence.
Local O2 catalog build median10.054ms; placement9.060ms. These are local decode
measurements, not Windows acquisition timing. Repeating GPU copies, VMA buffers
and decode per consumer is a concrete remaining acquisition hotspot.

- [x] Pin the production texture-stage/retirement path with two draws consuming
      generationA and another consuming overwritten generationB; preserve all
      three independent event payloads while requiring only two owned readbacks.
- [x] Increment the texture's content generation on every actual upload/copy into
      that image. Match allocation incarnation, content generation, capture batch,
      format/extent, mip/face and component mapping; never match by handle alone.
- [x] Share pending payload ownership with explicit references; decode once after
      its fence. Consumer metadata/state remains independent. Cancellation, mixed
      batches/generations and budget failures release exactly once; no silent gaps.
- [x] Keep lookup bounded/indexed and staging/reservations truthful. Disarmed
      recording performs no payload copy/hash/query. Native stage tests and actual
      Vulkan product flags precede exact-head CI and a fresh Windows driving run.
- [ ] Measure fresh pose delivery separately from display cadence. Further
      acquisition scheduling work follows measured remaining latency.

Native attempt4 atb19c9415 revealed a reuse-pressure regression despite both
CI triggers20/20 and190unitpasses: conservative pending CPU reservations
exhausted before the physical staging trigger. Retained failed manifest and
canonical run; no car gate/marker. Corrected tracking drains on either physical
or pending consumer bytes at the same safe completed-flush boundary. Two
runtimeREDs (pressure and24MiB-per-consumer accounting) then strict15 adapter,
ASan/UBSan and four actual-flags productTUs pass. New exact-head CI/native5 pending.

Native5 verified37-part captured car and native save/reopen at8b316278, but
first follow request still captured all1723parts and stopped on a partial frame.
RefreshFilter ran only on later rearm. Seed the first request from the pinned
selection; retain unfiltered ordinary discovery and later refresh. RuntimeRED
0vs1 stage sets; all18 strict/sanitized controllercases+2productTUs pass.
8bpushCI20/20;PRmacOSarm64DNSfailurebeforecompile retained. New native6 required.

### Native6: continuous bounded selected-frame acquisition

Exact e90 CI passed both20/20. Native selected follow is42parts and original
37-stage assembly renders above100HUD FPS; freshposes4.5/s before mismatch.
Saved frame proves evolving position bytes and one damaged part split, not
merely missing topology. Shared acquisition records80copies/895shares in
the51-part final frame. Failed/incomplete run6 archived, no marker.

- [ ] Explicit LiveDrawInputs continuous flag retains at most3 frame starts,
      preserves pending GPU ownership, and exports only fully completed closed
      frames. Forensic acquisition/retention remain unchanged.
- [ ] Selected live worker consumes latest completed frame while acquisition
      continues; no50ms completion polling or post-publication33ms rearm gap.
      Selection/filter/reset/cancellation and budgets stay owned and bounded.
- [ ] Diagnose deformation and split-range correspondence from owned native
      evidence; retain ambiguity rather than silently selecting another car.
- [ ] Strict/sanitized and actual-head CI, then fresh immutable Windows native
      straight-driving/wheel/steering and cadence/freshness qualification.
