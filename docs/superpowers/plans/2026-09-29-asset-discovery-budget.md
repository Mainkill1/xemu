# Asset discovery budget recovery implementation plan

> **For agentic workers:** Use superpowers:executing-plans for this correction in the existing PR260 worktree.

**Goal:** Make fresh PGR2 asset discovery drain pending copies before recorder memory exhaustion and explain capture limits and car assembly inside the browser.

**Architecture:** Keep the existing bounded owned-input recorder. Query its remaining CPU capacity at the existing safe Vulkan draw boundary; preserve the first terminal reason and the last coherent asset frame if a later frame fails. All real limits and GPU fence ownership remain enforced.

**Tech Stack:** C/C++17, Vulkan, ImGui, GLib focused/native fixtures.

**Spec:** docs/superpowers/specs/2026-09-28-pgr2-live-assets.md

## Global constraints

- Keep PR260 additive to PR259; no new recorder, guessed player IDs, budget increases, or shared guest state changes.
- Captured copies live until their actual submission fence; draining is outside draw/debug scope.
- Real CPU/event/per-draw exhaustion remains explicit; forensic acquisition never silently drops evidence.

## Review focus

- Recorder resident bytes plus shared-copy reservations can exhaust memory below the fixed staging threshold.
- A later generic adapter error must not hide the initial terminal limit.
- An unsuccessful later frame must not erase an already usable complete discovery frame.
- Retry must retain shared-recorder ownership checks and current configured budgets.
- First-time users need selection, suggested membership, assembly, follow, and shader inspection steps.

### Task 1: Bound readback pressure by remaining recorder memory

Files: shader-browser-capture-session.hh/.cc, shader-browser-draw-inputs.h, shader-browser-draw-request.cc; vk/shader-browser-input-pressure.h and draw.c; native texture-reuse and pressure tests.

- [x] Add a real native fixture with retained CPU input blocks and shared texture consumers below the fixed 128 MiB threshold; observe the drain predicate fail before a subsequent reservation exhausts the session.
- [x] Expose CaptureSession::ReadbackPressure(uint64_t headroom) and xemu_shader_capture_session_readback_pressure(uint64_t headroom); read accounting under the recorder mutex without copying snapshots.
- [x] Feed pressure into the existing safe-boundary predicate only when GPU payloads are pending. Reserve kDrawInputBudget plus metadata headroom for the next draw.
- [x] Verify native resource-version/fence tests and unsafe-boundary tests, including inactive recorder.

### Task 2: Preserve discovery evidence and explain recovery

Files: capture-session.cc, asset-browser-live.cc, asset-browser.cc; controller tests; docs/devel/asset-browser.md.

- [x] Reproduce overwritten first exhaustion reason and loss of an already complete catalog in runtime tests.
- [x] Keep the first terminal reason, include accounting and exact failure in discovery status, and retain the last complete catalog after a failed later capture.
- [x] Add visible ordered car-selection guidance and a retry action that uses the existing start path/current budgets; keep failed data in the recorder for diagnostics.
- [x] Run strict/sanitized component tests, original forensic tests, format, and actual translation-unit builds.

### Task 3: Qualify fresh native discovery and publish correction

- [ ] Obtain green exact-head CI and build artifacts on the existing draft PR260 branch.
- [ ] Run a new immutable Windows test with no saved assembly, default settings, car selection/assembly/follow, and capture save/reopen; preserve failures and separate display cadence from fresh poses.
- [ ] Publish a corrected testing prerelease only with verified artifacts and documented remaining limitations.

### Task 4: Frame captured shader output and expose inspection controls

User report adds frequently empty/out-of-view assets and a Ghoulies spider death-animation discovery case. This continues the approved post-transform inspection requirement in the PGR2 specification; it does not infer engine identity or silently substitute fixtures.

- [x] Reproduce a captured nonlinear/translated vertex shader outside raw input bounds in the actual GL viewport.
- [x] Measure referenced shader-produced positions using bounded transform feedback when no host GS is present; cache by immutable inputs and restore touched HUD state.
- [x] Fit those outputs as an explicitly labeled projected view when direct placement cannot be established; preserve direct-matrix orbit mode for the live car.
- [x] Expose Fit, standard angles, zoom/pan controls and guidance; do not carry an offscreen pan into a newly selected occurrence.
- [x] Make captured occurrence output accessible from the shader workbench without requiring synthetic inputs.
- [x] Verify supported offscreen/nonlinear outputs, changed constants, zero/nonfinite w, budget rejection and HUD state restoration with actual GL fixtures.
- [x] Restore the newest user-provided Ghoulies snapshot through the upgraded tester API and retain an owned recording on Windows. Discovery produced 909 parts without the former budget failure; no spider identification is established.
- [x] Reproduce the native host-GS framing rejection and extend fitting to bounded triangle-input GS emissions. Verify triangle/line/point outputs, expansion, empty emissions and budget rejection with actual GPU fixtures.
- [x] Reproduce and correct generated pixel-shader depth clipping against old window coordinates after the inspection camera moves GS output.
- [ ] Recheck the geometry-stage correction with sanitizers, actual product compilation and independent review; publish it to the existing branch and qualify an exact-head Windows artifact.
- [ ] Qualify native discovery and the provided Ghoulies snapshot after the supported tester import route is installed.
