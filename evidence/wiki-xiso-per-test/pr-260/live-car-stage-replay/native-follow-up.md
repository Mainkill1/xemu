# Native connected-car follow-up

Exact Windows CI build: `5bd982499ce13800aafb23380f5e99352dcab09f`,
executable SHA256 `38de23b96f6d95c6bb47fcaef2d930f76e4215c9460164e59cace929a1ba1ade`.
Push36511947092 and PR36511950908 completed successfully,20/20 jobs each.

The HUD context correction passed the real application boundary. In the PGR2
Windows Vulkan race, body E7152/frame3851/draw6997833/emission3489322 was
identified using1522vertices/2656triangles, the recognizable body and its four
owned texture slots. A manually confirmed37-part assembly renders original
VS/PS/host-GS and material inputs. Straight throttle and left steering were
recorded in bounded3-second intervals with11 synchronized UI/game samples each.
The body anchor remains fixed; a front-wheel pose changes in the steering
sample. Grouping and subsequent correspondence remain inferred, not engine IDs.

The result **does not pass the complete requested gate**. During live capture,
sampled HUD readouts are31–38FPS and selected pose acquisition2.1–2.4/s. The
frozen view exceeds180FPS. These HUD values are short diagnostic observations,
not a matched gameplay benchmark or30 fresh poses/s qualification. Later
incomplete correspondence correctly retains the last complete frame instead
of mixing poses. The default angle exposes underside geometry and the colors
are darker than the game display. No acceptance marker was written. The run
is archived correctness failed/evidence incomplete/comparison ineligible; its
owned process exited and no replacement rule was enabled.

Source inspection shows the game display applies the Xbox DAC palette after
rendering, whereas this build's viewer omitted that step. The follow-up retains
the palette at the capture boundary under its writer mutex and applies it once
after the shared assembly target is blended. The GPU regression fails on the
old renderer (raw blended red102 rather than owned LUT red201) and passes after
correction. Missing, duplicate, short or differing assembly palettes reject;
legacy absent palettes remain explicitly labeled pre-display. Strict and
ASan/UBSan viewport fixtures and five affected actual-flags production units
pass. Sanitizer leak detection follows the existing Mesa/GLX limitation.

The closer above-body default angle is independently demonstrated on the private
saved native frame. The next exact-build Windows test must verify the captured
palette and appearance; no native color success follows from the authored LUT
fixture. The next-frame acquisition/rearm/readback lifecycle and repeated texture
copies require profiling before a continuous pose-cadence claim.

Private game resource bytes, stage source, and native screenshots remain outside
public Git. An initial save failed because the diagnostic helper was given a
wrong Ctrl modifier; its failure is retained and the corrected save succeeded.
This is test-driver friction, not a product save failure.
