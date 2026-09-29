# Discovery limits and captured-output framing checkpoint

Corrects premature Vulkan capture budget exhaustion, failed-frame catalog loss,
and frequently out-of-view raw geometry. The existing draft PR260 is retained.

- Vulkan drains owned GPU input snapshots at a safe emission boundary when the
  actual remaining recorder budget cannot reserve the next bounded draw.
- First terminal failure is preserved. Discovery retains the previous complete
  catalog after a failed later frame, names the exhausted control/limit and shows
  ordered discovery/selection guidance and retry.
- Captured VS output is measured with bounded transform feedback for supported
  no-GS pipelines, cached by exact input bindings including image faces/mips,
  fitted with explicit projected-space labeling and precise unsupported reasons.
- Fit/F, standard angles, zoom and pan controls are visible. Thumbnails attempt
  captured shader output/materials before labeled raw-input fallback.
- Normal single-draw and forensic occurrence views open the same captured-output
  inspector. Owned immutable copies include canonical digests, payload accounting
  and the original request scope/frame context. Shader/scope changes invalidate
  fitted ownership even in Advanced Lab. No mutable renderer reread is added.

Local verification: controller26/model13/native adapter16 strict and ASAN/UBSAN
cases pass; original forensic capture suite passes; actual GL nonlinear/translated
output, changed-constant, zero-w, cubemap binding, HUD-state and material fixtures
pass. The32-use immediate-keyboard/foreign-recorder fixture passes. Single-draw
ownership/cancel/save-reopen33cases pass strict and ASAN/UBSAN. Eleven changed
product translation units compile with-Werror. Formatting and whitespace gate
pass. Leak detection is disabled for sanitizer execution under the traced host;
address/undefined-behavior checks remain enabled.

Independent review: binding metadata/cache-key and seamless cubemap state findings
corrected. Final bridge review found stale selection and missing save context;
both corrected. Save/reopen regression also exposed payload accounting and now
passes. Final narrow source review found no remaining Critical/Important/Minor
issue. These are local fixture/source results, not native game proof or30fresh
poses/s.

The older e560 published preview/native fresh PGR2 discovery failure remains
historical evidence. Windows Ghoulies validation requires the provided QCOW2
snapshot carrier through the repaired supported tester API. Exact-head CI/native
PGR2 and Ghoulies validation and a corrected prerelease remain pending. No real
engine object ownership, complete GS replay, game-accurate free-camera shading,
spider identification or corrected release is claimed here. Raw game captures,
disks and screenshots stay outside the public evidence directory.

Agent assistance: Codex / GPT-6.

## Host geometry-stage follow-up

The newest supplied Ghoulies snapshot was restored through the upgraded tester
API. Default discovery retained 909 parts without budget exhaustion. Selecting a
captured host-GS occurrence reproduced an explicit unsupported framing error.
Its owned recording stays on the Windows tester for native revalidation.

Final-stage transform feedback now measures actual triangle-input host-GS output
with bounded triangle, line and point expansion. The camera is applied at the
final emission boundary. Empty emissions and oversized output remain explicit
failures. Generated pixel depth clipping now permits both signs while the
inspection raster supplies depth; the existing depth fixture failed before this
correction and passes afterward.

Strict and ASAN/UBSAN full GPU fixtures pass, including expansion, empty output,
readback budget rejection and existing material/depth/HUD state cases. Four
changed product translation units, including the controller settings view,
compile with -Werror. Independent source review reports no remaining findings.
Leak detection is disabled under the traced host; other sanitizer checks remain.

Native testing also recorded a controller settings null dereference after a
snapshot reconnect. A separate guard commit disables mapping reset when no host
controller is bound. Its native recheck is pending the corrected Windows build.
The private crash dump, game recording and screenshots are not included here.
No spider identification, native corrected rendering, exact replay, merge-ready
status or corrected prerelease is claimed by these local fixtures.
