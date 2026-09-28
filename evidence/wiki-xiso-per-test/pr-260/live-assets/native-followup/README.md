# Native Windows discovery follow-up

The ca6dd exact-head Windows PGR2 attempt reproduced incomplete Vulkan discovery
at both 256 and 512 MiB CPU retention. The owned partial frame held 3426 events,
176 decoded parts and 31,374,436 unique block bytes. Pending per-draw readbacks
exhausted the distinct 256 MiB GPU/CPU staging pool before the normal frame fence;
immutable deduplication runs only after that fence. No car recolor POC passed.

This follow-up drains existing submissions after completed draw flushes at
128 MiB pending staging, using the existing fence/retirement path. Mid-scope
flushes qualify; open draws/debug scopes do not. Timing publication precedes the
wait. Disarmed capture has zero pending bytes and cannot trigger this path.
A single oversized multi-range flush can still fail explicitly; universal
capture coverage is not claimed.

The requested purple checkerboard is drawn once behind the viewport/thumbnail
meshes, with the same diagnostic material colors and GL state restoration.
The native GL fixture checks alternating empty pixels, foreground material
pixels, thumbnail pixels, cache limits and caller state.

## Verification

- RED checkerboard regression failed on the former gray pixels: blue17 versus
  required green+30 (41). GREEN native SDL/OpenGL fixture passed afterward.
- RED staging regression exhausted at the sixth successive 50 MiB draw.
  GREEN preserved all32 draws within256 MiB, with safe-boundary guards.
- Actual configured product compilation passed for the changed viewport and
  Vulkan draw translation units (warnings treated as errors).
- Native viewport ASan/UBSan behavior passed. Default LeakSanitizer reported
  194140 bytes in Mesa GLX driver allocations; the established fixture setting
  detect_leaks=0 passed. No leak-free process claim is made.
- Complete current-head CI and Windows PGR2 follow-up remain publication/native
  gates; their exact identities/results will be recorded separately.

No private game resource bytes are included in this public evidence directory.

Focused independent code review found no Critical/Important defect. Reviewer
independently ran the pressure test and checked existing fence retirement and
subsequent preparation. It correctly notes that the policy regression simulates
retirement; native finish/resume and capture completeness remain unqualified.
