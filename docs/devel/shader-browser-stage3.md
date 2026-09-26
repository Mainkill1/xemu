# Shader Browser Stage 3 — replacements and runtime overrides

Stage 3 intentionally changes rendering. It is stacked on the Stage 2 Shader Details branch and must remain a draft until both renderers pass native output, failure, lifetime, and overhead validation.

## Research basis

The design was rechecked against six primary-source implementations:

1. Cemu graphics packs: title/stage-aware custom shader selection and renderer-format compatibility.
2. 3Dmigoto ShaderOverride: skip and additional partner/resource targeting instead of assuming every use of one shader is interchangeable.
3. ReShade's shader-replace example: replacement at pipeline/shader creation with explicit replacement-byte ownership.
4. RenderDoc Vulkan replay: replacing a shader requires rebuilding dependent pipelines.
5. Mesa GLSL/SPIR-V replacement: separate replacement inputs from disposable dumps and include replacement content in cache behavior.
6. Dolphin's custom pipeline design: useful semantic-interface direction, but the inspected draw action remains incomplete and is not treated as end-to-end proof.

The shared lesson is that xemu needs a guest-scoped policy layer and backend-owned construction, activation, and retirement. An external OpenGL/Vulkan interception layer would discard the guest identity and state that xemu already owns.

## Stage 3 v1 scope

- Session and optionally saved rules scoped by full TitleID + ShaderKey, with optional executable fingerprint and typed draw restrictions.
- Deterministic precedence and explicit equal-precedence conflict reporting.
- Normal, Force Uber, Force Specialized, Skip Draw, Highlight, and Replacement actions, but only advertise actions actually supported by the active backend.
- Full pixel/fragment replacements first. Vertex/fixed-function/geometry replacements require separate stage-linkage fixtures.
- Separate replacement payloads for OpenGL and Vulkan may share one portable target manifest.
- Requested and effective actions remain separate while candidates prepare.
- Replacement failures are recoverable; they never use the existing fatal internal compiler path.
- SQLite/filesystem work is excluded from normal draws. The active title/build produces an immutable/read-mostly in-memory index.
- Replacement content identity participates in effective program/pipeline cache identity.
- Existing compatibility fixes remain distinct from user overrides.

## UI interaction

The compact Shader Details panel owns the override action, selected replacement, scope, reload, and requested/effective state.

A selected replacement activates compatibility guidance in the left shader list:

- compatible shaders receive a subtle highlight;
- incompatible shaders remain visible and expose a reason;
- each row is a drag source carrying the full ShaderKey and explicit active TitleID;
- the Replacement area is a drop target;
- dropping assigns the replacement to that shader after full compatibility validation.

Drag/drop is an accelerator, not the only workflow. The default accessible path remains: select a shader, select a replacement, then press Apply. Keyboard navigation and context-menu assignment must remain possible.

Short display IDs are never carried in drag payloads or used as keys.

## Runtime flow

```text
UI / import / optional SQLite rules
    -> validate and own source outside the renderer
    -> rebuild active-title in-memory index
    -> shader binding/pipeline preparation resolves a policy
       -> OpenGL builds a separate linked program with fresh locations
       -> Vulkan builds compatible module/pipeline variants
    -> publish newest compatible candidate at a renderer boundary
    -> draw uses the attached effective policy and compact predicate
```

A rule generation invalidates cached binding policy even if guest shader state is clean. Ordinary draws do not scan rules, parse files, query SQL, encode IDs, or hash source.

## Replacement identities

- Target identity: explicit title/build scope + identity version + stage + full 96-bit ShaderHash + optional typed draw restriction.
- Replacement identity: immutable source/includes + entry point + options + interface ABI.
- Effective host identity: target host interface/pipeline state + replacement identity + backend/generator ABI + route and compile decisions.

Changing file contents under the same path must never reuse an old effective program or pipeline.

## Storage

```text
<config-dir>/shader-browser.db       optional rule metadata
<config-dir>/shader-artifacts/       disposable generated/compiled artifacts
<config-dir>/shader-replacements/    authored/imported source and manifests
<config-dir>/shader-presets/         portable rule preset exports/imports
```

Authored replacements must survive artifact cleanup. Database-off operation still supports session-only rules.
Portable presets contain full title/build shader identities and replacement
package IDs, but no authored shader source; package directories must be shared
separately. Importing a preset creates process-lifetime imported rules.

## Validation gates

- wrong title/build, hash collision, and equal-priority rules cannot select the wrong action;
- changed source under one path cannot retrieve a stale program/pipeline;
- bad source/link/interface errors preserve the last valid effective object and do not abort xemu;
- a valid replacement may bypass a deliberately broken original before the original is required to compile;
- A/B/C out-of-order preparation publishes only C;
- policy changes take effect while guest state remains clean;
- pending logical draws, recorded command buffers, and submitted work retain correct object owners;
- skip preserves guest progress, reports, synchronization, surface bookkeeping, and unrelated clears;
- reset, save/load, title/backend switch, renderer recreation, and shutdown reject obsolete work safely;
- the drag/drop and button workflows produce the same validated rule;
- disabled rules preserve normal output/routes and create no replacement work;
- native cold/warm measurements report frame-time tails, creation counts, waits, memory, output, and guest progression.
