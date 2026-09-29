# Native HUD context correction

The c5cd539546 Windows Vulkan PGR2 run captured the body geometry and all
four bound texture slots, including sampled Y16. Selecting the body failed
with `Captured generated stages require OpenGL 4.5 in the HUD`. The HUD
requested 4.0; the Windows driver returned exactly that version. The previous
local GPU fixture requested 3.3 but received Mesa's newest available version,
so its successful shader compilation did not cover the application boundary.

The HUD now tries 4.5 and falls back to the existing 4.0 minimum. Global
context attributes return to 4.0 before any other context creation. No guest
renderer context requirements changed. The native viewport fixture uses the
same negotiation helper as the application.

The preferred-version regression failed at runtime before the correction
(`red.log`). Three strict C negotiation cases pass: capable driver, fallback,
and total creation failure. The complete native viewport fixture and its
ASan/UBSan build pass (`viewport*.log`). Sanitizer leak detection is disabled
for the existing Mesa/GLX allocation limitation; no leak qualification is
claimed. Actual product flags compile `ui/xemu.c` with `-Werror`.

The failed native run remains archived with correctness **failed**, evidence
**incomplete**, comparison **ineligible**, and no acceptance marker. Its owned
xemu process exited. Both CI runs on c5cd539546 completed successfully, 20/20
jobs each. Those checks predate this correction. A new exact-head Windows
artifact and immutable native test are required before car appearance, wheel
motion, assembly following, or 30 FPS can be claimed.
