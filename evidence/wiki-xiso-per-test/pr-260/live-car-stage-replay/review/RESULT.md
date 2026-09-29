# Review correction gate

All five Important findings and the Minor count-key finding are fixed in one
bounded pass. No findings are deferred. The standalone actual GL source-shape
placement assertion failed before the correction and passes afterward. Export
round-trip failed with restored anchor70 instead of selected80; all3 cases now
pass, including explicit non-first anchor and legacy annotations.

Native GPU regressions fail on the previous implementation: clipped near-half
(assert134), duplicate null image (crash139), changed cached count (assert134),
legal GL maximum level1000 (assert134). The final complete viewport suite passes,
including extra/equal-count duplicate null/undersized images and generated
non-perspective barycentric clipping on both sides of the inspection center.
ASan/UBSan also passes (Mesa/GLX leak checking remains disabled as documented).
All actual-flags changed UI/GL translation units compile with -Werror.

The explicit version2 anchor survives emission sorting, save/reopen and UI
reconstruction. Version1 archives retain their previous ordering semantics.
Only validated unique texture images are uploaded. GL maximum-level clamping
preserves rejection of missing mip chains with mipmapped filtering. Mesh cache
keys now contain active-stream count/offset/slot metadata.

Native Windows connected PGR2 car, wheels, captured shading, pose freshness and
30FPS gate remain pending. No new CI or native-title success is claimed here.
