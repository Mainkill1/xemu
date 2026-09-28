# Asset Browser

Open **Debug → Asset Browser** while a game is running. Enable **Live discovery**
to acquire bounded frames without pausing the guest. Discovery uses the same
capture owner as Shader Browser; another active recording must finish first.
Closing or collapsing this window stops only its own acquisition and releases
GPU caches. Freeze retains owned inputs.

## Find and retain a car

1. Browse the thumbnails, or use Up/Down, Page Up/Page Down and Home/End to inspect
   captured entries immediately. The filter and largest-geometry ordering help
   narrow the list. Draw entries are not game asset names or proven objects.
2. Freeze discovery while identifying the body, wheels, glass and other parts.
   Check their entries, enter a name such as **My car**, then choose
   **Assemble checked parts**. The named assembly remains in the left list.
3. Orbit with left drag, pan with right drag, and zoom with the wheel. All parts
   share one frame and one bound. Wireframe, captured texture slots, vertex color
   and clay are diagnostic display options.
   A purple checkerboard behind models and thumbnails makes dark silhouettes
   easier to distinguish. It is an inspection background, not a captured texture.
4. Unfreeze and enable Live discovery/Follow to search for a unique geometry
   correspondence. Matching is inferred. Identical cars, repeated draws, changed
   geometry/LOD and missing parts retain the last coherent view with a status;
   they must not silently select an opponent.
5. Freeze the selected occurrence and inspect its captured texture images,
   constants, raw streams and generated stage sources in the right inspector.
   **Inspect exact draw / GLSL** opens that part and its retained frame in Shader
   Browser. Its supported replay/editor flow can test an edited shader; inspection
   and saving do not activate a game replacement.

The generic browser has no verified PGR2 player-car resolver. A user label does
not recover a model filename, skeleton or engine identity. Manual membership is
the supported way to establish an assembly at this checkpoint.

## Save and extract

Enter a new directory or GLB filename in the file path control. Existing
destinations are refused. File work runs asynchronously and can be canceled before
publication.

- **Save captured frame:** preserves the selected frame's immutable events,
  resources and assembly annotation, including the other uses of its shaders.
- **Extract selected inputs:** preserves only selected occurrences and their
  inputs. It explicitly lacks the complete ordered frame/dependency closure.
- **Export GLB:** exports decoded triangles, raw UVs/colors, supported captured
  base textures and fidelity/provenance metadata. It is a diagnostic mesh export.
- **Open capture:** reopens owned data without reading resources from the current
  game. The saved selection is restored and frozen.

## Fidelity and limits

| Component | Asset Browser view |
|---|---|
| Geometry | Referenced captured vertex inputs and emitted topology |
| Vertex processing | Raw positions; original transforms/skinning not evaluated |
| Texture | Supported owned host-decoded 2D base image; raw T0 UV |
| Material | Diagnostic texture/color shader; original PS math not evaluated |
| Constants/stages | Owned words and generated sources available for inspection |
| Exact draw | Existing Shader Browser replay, within its interface/budget support |
| Assembly tracking | Unique geometry candidate; duplicates remain ambiguous |
| Engine identity | User-confirmed membership/label; no automatic player identity |

Original generated UVs, multi-texture operations, render-state blending, destination
contents and animation pose can differ from this input view. Unsupported data
retains its reason and raw capture evidence. The full recording remains the source
of truth; GLB is not a complete replay package.

Default asset limits are 32,768 examined events, 2,048 parts, 1,048,576 vertices,
3,145,728 indices and 128 MiB decoded data. Live sampling has bounded capture
budgets and a progress watchdog. Named assemblies, GPU meshes, texture inspection
and thumbnails have separate limits. Exhaustion reports partial data instead of
claiming a complete fresh frame.

Vulkan retires pending capture readbacks at completed draw-flush boundaries when
staging pressure reaches 128 MiB, within its separate 256 MiB staging limit. This
prevents repeated texture copies from exhausting staging before the normal frame
fence. Acquisition may wait for those submissions; this is capture overhead,
not shader execution time. A single oversized multi-range flush can still exceed
the limit and remains an explicit incomplete capture.

**Capture settings** exposes bounded memory, event, part, vertex, index and
sampling limits; stop Live discovery before changing acquisition settings. GPU
mesh and thumbnail limits can also be changed there. Named full recordings share
a 256 MiB retention budget and named decoded geometry shares 128 MiB. Recalling a
named assembly recalls its original recording and dependencies. Old packages
without explicit frame completion evidence remain browsable but are not labeled
complete frames. A manually stopped frame or failed readback is also incomplete.

HUD cadence and capture age describe different work. A local trivial GL fixture
exceeds 30 FPS, but PGR2 viewport cadence and capture-on gameplay overhead require
measurement on the target hardware. Discovery is opt-in and disarmed by default.

## Native validation still required

Test PGR2 Vulkan and OpenGL while racing: recognize and assemble the player car,
inspect textures/motion, follow versus same-model opponents, change LOD, freeze,
open exact GLSL, save/reopen, reset and close. Record viewport cadence and matched
capture-off/on gameplay overhead separately. Do not treat the authored 32-draw
fixture or a mechanically mergeable PR as this native qualification.

The current HTTP tester's saved race procedure does not automate the Asset Browser
controls. These interactive gates need human execution or a reviewed native UI
test procedure; remote-shell workarounds are not part of normal test operation.
