# Shader workbench design for PR #241

Source of requirements: [Mainkill1/xemu issue #244](https://github.com/Mainkill1/xemu/issues/244), confirmed by the user for full inclusion in PR #241 on 2026-09-26. This design supersedes the former PR #241 one-shot game-draw replay task. The workbench uses synthetic or explicitly imported, owned inputs; its output is labeled "Synthetic inputs — not an in-game draw". No automatic or one-shot game-draw capture is part of this PR.

## Purpose and boundaries

A person can select a shader observed by xemu, inspect its generated GLSL, test it on a camera-adjustable 3D object within a small reference scene, edit a private draft, compare Original and Edited under identical conditions, and save/reopen the experiment. Only an explicit Stage 3 action may change game rendering. The canonical guest recipe, renderer shader cache, draw statistics, and mutable game resources remain separate from the workbench.

The existing external Shader Browser window, PreviewService governor, three leased output slots, synthetic clock/fixtures, and private GL/Vulkan executors are extended. No second compiler, clock, renderer, database, or profiler is introduced. Pixel shaders are the first executable stage class: vertex and geometry sources remain inspectable/exportable when their complete input and companion interface cannot be executed privately. Unsupported requirements are named in UI and exports.

## Shared 3D scene

PreviewScene becomes a versioned, fixed-size description: central target (quad, sphere, or cube), camera yaw/pitch/distance and logical target pivot, plus four built-in references: patterned backdrop, ground plane, intersecting marker, and foreground blocker. Each reference has visibility and a bounded translation. Scene reset restores defaults; Focus target resets camera pivot without changing the mesh or draft. No arbitrary asset hierarchy is stored.

One geometry builder applies a common camera/view/projection to all objects and returns bounded vertices plus explicit draw ranges and object roles. The backends render every role into one private color/depth pass for each result. Reference geometry uses a fixed private material; only the central target uses the selected shader or edited draft. Opaque references establish depth, then the target uses the declared synthetic blend/depth/cull/alpha settings; exact order is documented and tested. Viewport resize changes aspect and render identity but never source compile identity. Output extent is bounded to 128–640 by 128–480 pixels; resource allocation is checked before admission. Missing private depth support is Unsupported with a reason.

Camera orbit, pan, and dolly use input only while the preview viewport owns focus/hover in the external or embedded browser. F focuses the target; Reset camera and Reset scene are explicit. The preview gestures must not reach guest controls. An invisible/discarding target retains its logical pivot. Existing 2D image magnification, if retained, is separately labeled and never substitutes for 3D camera movement.

## Source, draft, and preview identity

GeneratedSourceSnapshot owns the selected detached HostSource GLSL and its full ShaderKey, exact title/build scope, backend, stage, route, resident/reconstructed provenance, generator/interface ABI, and digest. OpenGL reconstructed source is labeled reconstructed; Vulkan resident module source is labeled resident. Generated GLSL is xemu's translation, not game-authored code. The snapshot and canonical recipe are immutable.

A WorkbenchDraft is an authored copy with independent ID and monotonically increasing revision. Editing never mutates generated source or creates a Stage 3 replacement rule. Manual Compile or a 400 ms auto-compile debounce submits only the newest immutable draft snapshot through PreviewService. The draft identity and source digest belong in the compile key; camera, fixture, clock, render-state values, and viewport belong in the result key unless a Vulkan static pipeline state requires private re-preparation. Service stale-result and resource admission rules remain in force. The UI records attempted and displayed draft revisions; invalid code keeps the previous successful image and labels it stale, with bounded compiler diagnostics and line links.

The optional lower editor dock provides line numbers, GLSL token highlighting, search, source-line diagnostics, Manual Compile, and auto-compile toggle. Parsing/highlighting is incremental or viewport-bounded for source up to the existing 4 MiB source cap. The source viewer, editor, and errors all use the same immutable source-line offsets; compiler logs without a reliable line number remain readable as plain text.

Original/Edited toggles a source variant over one frozen experiment input snapshot (scene, camera, fixture, bindings, clock time, synthetic render state, initial destination). Comparison never changes the game. A previous arbitrary Freeze Reference image can remain separately available, but cannot be labeled an equal-condition Original/Edited comparison.

The existing synthetic clock drives only declared, saved bindings such as color/alpha, UV transform, constants, and fog. If the shader does not consume a binding, the preview may remain static. Inputs and texture/sampler resources show Synthetic, Imported/owned, Substituted, or Missing provenance. Unsupported varyings and resource types stop execution clearly.

## Export, table, and explicit game action

Copy/Export generated GLSL copies detached source bytes and exports a provenance sidecar, separate from Stage 2 canonical recipe export. Usage CSV/JSON is formatted from one copied Snapshot, with recorded counts/recency and nullable measurements; it never invents timing or draw state.

A versioned xemu.shader-workbench-test.v1 bundle is an owned, bounded JSON experiment. It stores full shader identity and exact title/build scope, canonical recipe bytes, generated and authored source plus companion source, backend/interface metadata, fixture and owned texture data, reference transforms, camera, named bindings, clock state, synthetic render state and destination clear, dependency provenance, and source/content revisions. Parsing validates schema, sizes, digests, finite floats, stage/interface/backend compatibility, and paths before normal preview admission. Reopening an experiment is offline and never applies a game override. Aggregate owned data stays within 32 MiB and individual source text within 4 MiB.

At 1024x720, the left table shows ID/user label, draw count, and last-used age without horizontal scroll; the right viewport remains prominent. Search, recent-frame threshold, pin, hide/unhide, and Show hidden preserve stable selection. Hide is only a browser filter. The editor is hideable below and controls live in collapsible drawers. Source status and game-override status are distinct.

Apply in game explicitly packages a compatible successful pixel draft through Stage 3's authored replacement mechanism, with pixel interface ABI 1, exact title/build/full shader key and backend checks. The action displays requested/effective revision and rule scope. Restore original removes only the corresponding workbench rule and leaves the draft/experiment intact. Preview compilation alone never writes an override.

## Verification and release

Implementation is split into scene, editor/service, and export/table/Stage 3 lanes with isolated worktrees and reviewed interfaces. Local focused checks may run when a concrete risk requires them. There are no intermediate GitHub pushes or CI runs. After full integration, validate the complete unit suite, native GL and Vulkan scene pixels, camera/paused input isolation, editor success/failure/rapid-edit recovery, export/reopen, reset/close/reopen resource bounds, reduced-size layout, and matched gameplay preview-off/on behavior on Steam Deck and Windows hardware. The build machine may compile only. Run CI once on the final integrated head, update the PR declaration/evidence, then mark #241 ready for user validation. PR #242 follows.
