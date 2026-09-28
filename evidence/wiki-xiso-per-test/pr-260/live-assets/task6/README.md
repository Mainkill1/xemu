# Reviewed checkpoint verification

The logs cover authored capture/model fixtures, local native SDL/OpenGL/ImGui
fixtures and an existing capture-session regression suite. They do not establish
native PGR2 recognition or original appearance. See `REVIEW.md` for findings,
corrections, independent recheck and explicit executor rulings.

Local final results:

- 11 strict/sanitized model cases, including missing evidence, failed inputs,
  packed/current GL values and repeated compressed Vulkan attributes.
- 13 strict/sanitized controller cases, including early Stop, retained full-frame
  Recall and overlapping named-assembly budget exhaustion.
- 2 strict/sanitized export cases; 32 independent uses retain differing geometry,
  constants and textures through save/reopen. fpng uses a normal separately built
  third-party object; feature/capture code is instrumented. Leak detection is off.
- Native viewport/configurable-cache fixture and native ImGui frame-ID/collapse/
  foreign-recorder fixture passed. The GL raw-stream test includes the production
  adapter and real native GL bindings, with resource-bookkeeping doubles only.
- Existing capture-session regression binary passed after boundary persistence
  changes. Changed production TUs and the GL draw adapter compile with actual
  configured product flags and warnings treated as errors. Generated/dependency
  headers are reused; full clean application builds are a CI gate.
- clang-format checks on feature files and `git diff --check` passed.

RED logs are genuine runtime assertion failures before their corresponding
fixes. The first GL test compile omitted stdio in its test harness; that was a
driver error, not a renderer failure. The UI collapse check was added after the
lifecycle fix; no separate collapse RED is claimed. Cache reconfiguration exposed
a deleted-bound-texture error and its native regression now passes.

Both native testers were reachable using their direct HTTP client. The saved PGR2
Vulkan race procedure has controller inputs/screenshots, no Asset Browser UI
steps, and forbids manual intervention. It cannot qualify this interactive
workflow. No native job, runner upgrade, baseline change or shell workaround was
performed. PGR2 Vulkan/OpenGL recognition/pose/LOD, viewport cadence and matched
capture-on gameplay overhead remain required human/native gates.

CI status and downloadable artifacts for the final published head are recorded
in the PR. Keep draft until remaining acceptance and stacked dependencies qualify.
