# Whole-feature review and executor decisions

One fresh read-only reviewer examined `203d1329..0831d074` in five passes:
ownership/controller, formats/topology, viewport/lifecycle, persistence/bridge,
and evidence/Meson. It independently ran 23 existing cases, found no Critical
defect, and requested changes for the following Important items.

| Finding | Correction and verification |
|---|---|
| Ready did not prove a full frame; failed readbacks looked complete | Persist an explicit terminating-frame boundary flag; manual stop and failed/invalidated inputs are incomplete. Early-stop RED and strict/sanitized controller/model checks. |
| OpenGL CMP streams lacked compression metadata | Capture the authoritative existing mask at the GL raw stream boundary. A native test includes the production adapter with real packed GL bindings/current attributes; mask RED and passing native run. Decode tests cover GL packed positions, decoded current normals and Vulkan repeated stride-zero packed normals. |
| Absent index/range evidence fabricated triangles | Known indexed draws require owned indices; GL array draws require captured subdraw ranges. Missing evidence retains the original event and reports Missing. |
| Named Recall lost the full frame | Named entries retain shared immutable recordings under a bounded budget, including resource evidence/descriptors; Recall restores that recording. Named-recording RED and budget/recall checks. Reduced single-event Shader Browser fallback explicitly lacks complete frame/dependency closure. |
| Automatic acquisition failures were hidden | Final live status is displayed independently of the enabled checkbox. Watchdog/ownership/budget messages remain visible. |
| Capture/cache budgets were fixed | Bounded, clamped Capture settings cover memory, event/part/geometry, sampling, GPU mesh and thumbnail limits. Native cache configuration RED and passing fixture. |
| Collapsed windows continued discovery/GPU retention | Collapse stops owned acquisition and releases GPU caches while preserving file work; the native UI fixture verifies deletion of queued viewport textures and survival of a foreign recording. |
| Native PGR2 acceptance missing | Retained as an open gate; draft is required. No title qualification is claimed. |

Minor issues were also addressed: row selection uses exact part ownership, open
clears selection/checks even for colliding frame/event IDs, keyboard navigation
scrolls the selected row into view, and a UI frame requests no more thumbnails
than the cache can retain before ImGui consumes them. Cache reconfiguration also
does not rebind a deleted returned texture name.

## Behaviors the reviewer declined to judge

The executor rules on all four entries:

- **Actual PGR2 recognition/pose/cadence/overhead:** required and still open. The
  HTTP tester is reachable, but its saved PGR2 race procedure has no Asset Browser
  UI actions and forbids manual intervention. No shell workaround or fake pass.
- **Automatic player identity/title adapter:** not implemented or claimed.
  Generic manual membership/labels are the authorized checkpoint; verified title
  evidence is required for stronger correspondence.
- **Separate post-transform Asset Browser renderer:** this checkpoint explicitly
  displays raw inputs and diagnostic materials. Original-stage replay reuses the
  exact-owned occurrence bridge; whole-car pose/material fidelity remains part of
  native acceptance and is not silently marked done.
- **Current-head CI/clean full application build:** required publication gates.
  Local changed-TU compilation is narrower. CI status is recorded separately for
  the final published head.

Task 6 is not fully complete until native PGR2 gates are qualified. These decisions
do not make the reviewed head merge-ready. AI-assisted implementation and review:
OpenAI Codex, GPT-6.

## Recheck

The same reviewer examined the fixes read-only and identified one further budget
path: following a named assembly could unshare parts from another retained name
and exceed the decoded geometry budget. A roughly 75 MiB shared fixture became
roughly 149 MiB unshared on the next frame; `shared-budget-red.log` records the
incorrect named-frame update. One deduplicating geometry budget check now serves
both Remember and Follow. Failure retains the prior named frame with a visible
status, while the live selection remains inspectable.

Final recheck independently reran 26 passing CPU cases: 11 model, 13 controller,
2 export. No remaining confirmed Critical/Important implementation defect was
found in the corrected working tree. Native title acceptance and final-head CI
remain required; the recheck does not authorize merge.
