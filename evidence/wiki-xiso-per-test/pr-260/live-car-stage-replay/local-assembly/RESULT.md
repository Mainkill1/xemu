# Local connected assembly implementation gate

Local fixture evidence only; native Windows PGR2 driving/fidelity gate is pending.
Base published revision b4c566; Task1 owned sampledY16 committed b80982.

-12 model and16 controller cases pass strict; both pass ASan/UBSan.
-2 export cases pass, including retained connected GLB node placement.
-Native GL assembly fixture executes owned VS/PS and exactR16 samples with body,
 two rotating wheels and transparent overlap. Shared depth, changed poses,
 original captured paint, explicit mip/LOD/border state, current draw texture
 budget failure, raw diagnostic placement and caller GL state pass.
-Existing native ImGui32-use immediate navigation/foreign recorder ownership passes.
-Actual retained PGR2 body VS/PS/hostGS compile and link using the final adapter.
 This is source-interface evidence, not observed car rendering.
-Actual-flags product UI/GL TUs and six Vulkan TUs pass -Werror.
-Existing forensic capture-session suite and native Vulkan preview pass.

Native GL ASan/UBSan passes with detect_leaks=0 after GLX/Mesa-only leak reports.
Sanitizer builds use O0 because GCC14 libstdc++ std::regex generates
maybe-uninitialized diagnostics at O1 instrumentation; production actual flags
remain warning-clean. No application leak qualification is claimed.

Captured-stage mode uses original owned programs/resources with one fixed
anchor-relative inspection camera. Guest pixel rounding, clip/depth bookkeeping
are overridden. Scene and destination dependencies remain incomplete. Texture
inspection uses decodedRGBA8 except exact Vulkan sampledR16 storage. Native
OpenGL R16 without exact storage is rejected. No full frame replay, automatic
player identification, per-car persistent replacement or PGR2 cadence claim.

Explicit LiveDrawInputs admission omits non-draw operations and marks missing
execution/dependency closure. Forensic NextFrame/Rolling admission is unchanged.
Selected stage pairings filter admission before payload copying; unsupported
matching draws remain evidence. Save/reopen, reset cancellation, ownership,
partial/duplicate poses and bounded storage are covered.

The local GPU throughput numbers describe toy cached fixtures. HUD cadence,
selected pose update rate and actual oldest draw age must be measured separately
on the Windows rig. Keep PR260 draft and preserve the draft259 dependency.
