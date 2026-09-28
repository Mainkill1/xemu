# Persistence and resource inspection verification

Authored fixtures, not a PGR2 capture. The two strict export cases preserve 32
uses of one shader with different vertex positions, uniform words and texture
bytes after save/reopen. A two-part named assembly round-trips separately with
an explicit incomplete-frame/dependency status. Invalid annotations, pre-cancel,
failed-operation completion and existing destinations are checked.

`export-red.log` records the original rejecting implementation. `control-red.log`
records the unfinished-control regression before its fix. `strict.log` and
`sanitized.log` contain the final passing runs. The export sanitizer driver
instruments the feature and capture sources; fpng is linked as a normal separately
compiled third-party object. Leak detection is disabled under this host's tracing.

The GLB test checks container alignment, bounds, accessors, indices and embedded
PNG pixels using an independent decoder. `independent-glb-import.log` records
Assimp importing two meshes and two embedded textures under the named root.
The writer follows [glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
texture origin and GLB alignment rules.

`native-viewport.log` checks captured texture orientation and direct texture
inspection with a local SDL/OpenGL context. `native-ui.log` checks immediate
keyboard selection and closing without stopping a foreign recorder.
`production.log` compiles all changed production translation units using the
existing configured product flags with warnings treated as errors; it reuses
generated/dependency headers and is not a clean full application build.

The Asset Browser renders captured vertex inputs, raw T0 UVs and a diagnostic
material. It does not execute original skinning, generated UVs, multi-texture
pixel operations or destination blending. Owned sources/constants are visible;
the exact part and its retained frame are passed to the existing Shader Browser
for supported original-stage replay. The GLB keeps these limitations in metadata.
The full capture package preserves raw evidence beyond the diagnostic GLB.

Native PGR2 Vulkan/OpenGL car recognition, motion/LOD, original appearance,
viewport cadence and capture-on gameplay overhead remain unqualified. A trivial
fixture's frame rate does not establish those gates. No automatic player-car
identity or model filename recovery is claimed.
