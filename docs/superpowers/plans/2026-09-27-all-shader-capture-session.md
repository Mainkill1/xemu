# All-shader capture session implementation plan

User-authorized scope from the all-shader capture handoff supplied in this session.
Continue PR #259; reuse its preview compiler, workers and scoped replacement library.

## Investigation identity

Shader definition -> many ordered draw occurrences. Each occurrence owns its exact
bound stage identities, geometry, state and immutable resource versions. A shader
filter never retargets capture. A draw or connected component is not an engine
object. Store raw bytes, including non-finite float bits, independently of a
safe preview decoder. Unsupported, suppressed and failed events remain evidence.

## Milestones and gates

1. **Session/event/state ownership.** Explicit Capture next frame and rolling
   animation recorder, guest-flip boundaries, mark and post-trigger completion.
   Declare CPU/GPU/event/disk budgets and actual retained coverage. Finalize pending
   readbacks before Ready; reset/title/save-state boundaries invalidate ownership.
   Disarmed paths perform no payload copy/hash/query/file I/O. Ordered draw and
   non-draw events expose missing instrumentation rather than inventing provenance.
   Deduplicate immutable bytes with byte-equality confirmation and resource
   incarnations separate from content identity. Budget exhaustion stops explicitly.
2. **Multi-use browser.** Dedicated Capture workspace, stable event selection,
   virtualized Uses table, frame range and shader/event filter, live keyboard
   navigation. Selected event drives output, stage source, resources, constants,
   timing and extraction together. Selection/visibility never cancels an armed
   session. No continuously rendering thumbnail fleet.
3. **Original resources/replay/export.** Persist and reopen independent owned
   recordings. Observed output differs from reconstructed replay. Preserve raw
   streams/layouts/current attributes, original stages/constants, textures/mips,
   samplers, raster state and destination dependencies. Unedited original-camera
   replay must match independent checkpoints for explicitly supported components.
   Do not substitute missing inputs automatically or call partial replay exact.
4. **Correspondence/diffs.** Within-frame input comparisons distinct from temporal
   tracking. User-confirmed tracks and evidence/candidate/ambiguous states. Never
   join across frames solely by shader, resource address or draw ordinal. Preserve
   dependency closure when rolling eviction would remove needed producers.
5. **Replacement validation.** Selected use and all matching uses share frozen
   inputs and report actual coverage. Producer changes propagate through dependent
   suffix replay; frozen old output cannot replace edited output. Save creates a
   source package only; explicit Enable applies to all matching shader uses unless
   a supported runtime condition narrows it. Requested/effective/fallback states
   and Disable remain visible.
6. **Validation/publication.** Native OpenGL and Vulkan support matrix, formatting,
   focused regressions, exact-head builds/CI, matched capture-off/on overhead, and
   user pre-release on existing PR. Keep draft until fidelity gates are met.

## First vertical slice

At least 32 draws sharing one PS but distinct geometry/constants/texture bindings:
record, stop, save, reopen, independently inspect by arrow keys, then run an edited
replacement across every matching use. Retain overwritten versions, malformed
inputs and suppressed events. This gate is an early demonstration; it does not
replace non-draw/dependency/full replay/temporal/native validation gates.

## Remaining acceptance campaign

Batched/multipass ownership; texture/constant overwrite and address reuse;
render-target aliasing, clear/copy/upload/resolve/partial/depth-only operations;
manual pre/post trigger with real retained range; recording while filtering;
reordered ambiguous temporal correspondence; checkpoint image/depth tolerance;
all-use defect/control comparisons and downstream producer/consumer case;
failed compile last-good revision; persistent enable/disable after restart;
CPU/GPU/disk/query budgets, cancellation, save-state/reset, close/reopen; matched
normal gameplay versus heavy diagnostic capture overhead. Timing is associated
complete draw interval, not stage-exclusive cost; capture and replay timings are
separate from ordinary live profiling.

## Bounded dependency replay after the batch foundation

Execute a selected producer and its retained consumers in verified resource
execution sequence, independently of CPU observation IDs. Maintain separate
original and edited immutable version stores. Only the edited branch's matching
seed draws receive the replacement source; downstream shaders use their original
captured source and the branch's actual newly rendered inputs. Never seed an
invalidated descendant version from its captured old snapshot.

The first supported representation is one sample, one layer, one base mip,
linear RGBA8 with explicit extent and channel/orientation mapping. Logical image
proof is separate from the raw allocation graph: it never converts unknown tiled
byte coverage or uncertain physical aliases into known provenance. A bounded
typed description links draw texture/color destinations and copy source/target
roles to exact finalized capture-local resource accesses. Unaffected external
inputs may enter only through explicit owned checkpoints associated with their
exact read versions. Missing internal closure, ambiguous alias, unsupported
format/mips/depth, missing destination or unknown commands yield Unsupported.

Reuse PreviewService, the existing compiler and native preview workers. After an
exact result-key completion, place the actual canonical RGBA output into the
branch store before building the next draw packet. Fully described same-format
image copies, CPU uploads and buffer copies execute with bounded exact CPU byte
semantics. Report reconstructed replay, without claiming GPU-copy timing or raw
tiled-memory fidelity. Clear/resolve require complete parameters and destination
proof; absent descriptions remain Unsupported.

Deterministic gates: producer -> copy -> consumer changes only the edited
consumer; captured descendant bytes cannot satisfy a missing produced version;
CPU IDs differing from execution sequence; independent original branch;
overwrites/reuse/subresource mismatch; cancellation/rearm and stale completion;
budget failure without partial output publication; absent/depth/alias/operation
proof; descriptor archive roundtrip. Native acceptance records an actual Vulkan
color vkCmdCopyImage surface-to-texture command and demonstrates that the edited
private native producer's real result feeds the private native consumer. Compare
the unedited sequence against an independent captured checkpoint. This remains a
bounded supported sequence, not a claim of complete frame replay.


## Coordination with additive Asset Browser PR #260 (2026-09-28)

Reviewed design head `035f0c4e144d7b7dd083c901d100f43efa38d25e`, stacked on #259. Asset Browser adds assembly and portable GLB export; the Shader Workbench remains responsible for occurrence diagnosis, GLSL editing, replacement preview, and explicit game activation.

- Share stable capture/session/frame/event/emission identities and immutable resource versions. An asset part links to its occurrences; selecting a shader filters all uses instead of replacing them with one latest asset.
- Use the full retained input streams, constants, stage sources, resource evidence and raw topology from `CaptureOccurrence`; the bounded diagnostic `OwnedDrawGeometry` is a preview representation and must not become the export format.
- Coordinate capture requests at the existing backend ownership/submission boundary. Independent window and export services can retain immutable snapshots without duplicate renderer reads or later mutable-handle access.
- Preserve original subdraws and primitive mapping for export triangulation. User-confirmed assembly and ambiguous temporal correspondence remain distinct from inferred geometry/resource similarity.
- Reuse the existing compiler, preview executor, replacement library and explicit Enable/Disable flow when sending an asset occurrence back to the workbench. Saving/exporting an assembly never activates a game rule.

This is a compatibility requirement and implementation handoff; PR #260 remains a design checkpoint and its GLB/assembly runtime is not claimed here.

## User-requested checkpoint boundary (2026-09-28)

Finish the current stability fixes and retain a reviewable draft checkpoint, then
return to Steam Deck performance work. Do not start additional recorder,
temporal tracking, asset assembly or replay-fidelity features during this
checkpoint. The acceptance campaign above remains the resume checklist; native
authored-scene and selected PGR2 passes do not satisfy every item or authorize a
merge-ready claim. Keep PR #259 stacked on #241 and preserve #260's additive
interface requirements.
