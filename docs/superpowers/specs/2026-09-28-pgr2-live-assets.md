# PGR2 live Asset Browser

Approved user direction: “find my car in a list while playing PGR2 and view it
in our live view”, followed by “so make it happen”. Extend the existing PR260
design with this end-to-end workflow. Existing performance Tasks1–3 stay saved;
this request resumes Asset Browser implementation only.

Use the current PR259 owned capture system, rather than another renderer recorder.
Asset Browser is an independent Debug window with recognizable thumbnail entries,
keyboard selection, multipart assembly, a textured orbit/pan/zoom viewport and
Live/Freeze controls. Gameplay continues during explicitly enabled discovery.
User-assigned labels permit “My car” without assuming engine filenames.

Each inspection assembly retains immutable events, material images, raw streams,
stage sources and provenance from a coherent frame. Parts can be added/removed
and confirmed by the user. Suggested grouping and cross-frame correspondence are
inferred and labeled. Duplicate matching candidates become ambiguous; retain the
last valid assembly instead of selecting an arbitrary opponent. Renderer/title/
capture generation changes cancel pending work. LOD replacements need supported
evidence or confirmation. Actual player identity needs a verified title adapter;
no guessed engine offsets are acceptable.

Discovery samples bounded complete frames using the shared capture owner, without
pausing gameplay. Never replace or stop someone else's capture. Process snapshots
off PGRAPH and the UI thread; coalesce stale updates. Configurable bounded CPU,
event, geometry and thumbnail budgets have explicit exhaustion status. Stop/close
releases the owned acquisition; freezing keeps immutable selected data.

The first inspectable representation is captured vertex input with captured texture
images and decoded attributes. Its material shader is a diagnostic interpretation,
clearly labeled, not an exact replay claim. Implement post-transform inspection
where owned stage/layout/constants permit execution; show precise unsupported
status otherwise. Keep the original game output and selected captured occurrences
accessible, with a bridge to the existing Shader Browser replay/editor.

Use a shared model bound for all assembled parts, never independently center each
part. Validate referenced indices, finite decoded values, topology and format
bounds; preserve source evidence when display decoding fails. Support the active
OpenGL/Vulkan host vertex formats and more than4096vertices within larger explicit
asset budgets. Avoid mixing different frame poses in a current assembly.

Use one cached live viewport renderer and on-demand cached thumbnails. Maintain
the existing at-least30FPS preview target; report actual viewport cadence, capture
freshness and capture-on gameplay overhead separately. The HUD owns GPU resources
and restores touched GL state. Teardown occurs while its context is valid.

Save/reopen the owned capture and assembly annotations. Export supported scene
geometry/materials/UVs/textures as GLB with warnings/provenance. Opening a part in
Shader Browser supplies its exact event inputs; saving/inspecting never enables a
replacement. Missing parts/pose/material effects remain visible.

Acceptance: native PGR2 Vulkan car discovery, recognition, multipart assembly,
pin/live motion, texture inspection, freeze, part-to-Shader Browser and save/reopen;
implemented OpenGL parity; same-model opponents, reordered draws, LOD/disappearance,
reset/close/budget and malformed-format regressions; native30FPS preview and measured
overhead. A32-instance fixture independently establishes instance separation and
coherent retained snapshots. Exact player naming/engine tracking is unqualified
until verified title evidence exists.
