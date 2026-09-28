# Clean-stage requested/effective profile follow-up — 2026-09-28

Integrates parent PR257 profile commit35bbc32243 while preserving PR258's original clean-stage guard/counter/config/UI code. Adds its typed clean-stage key to the formatter registry. Requested Auto is retained in the report; Disabled is effective immediately on Vulkan, and Auto is unavailable/effectively Disabled on OpenGL. No forced shortcut or changed dirty/DMA/palette/surface/lifetime guard is introduced.

## Verification

The merged source initially fails the complete-registry static assertion because the child adds a tweak; this expected missing-entry result is retained. Adding the key resolves that failure. Strict optimized GCC14.2/C++17 and ASan/UBSan each pass all five real GLib/TAP profile cases, including the child's Vulkan Auto/Disabled and OpenGL-unavailable states. Existing generated-config persistence/migration/default/live/restart tests pass. The standalone texture-binding helper test passes all seven cases, including eligibility, source identity, dirty re-enable, failed retry and descriptor publication retention. Source hash manifest and logs are adjacent. New test formatting and diff whitespace pass.

Local command reuses the parent driver with an isolated child output directory and freshly regenerates this checkout's xemu-config.h:

```sh
python3 /tmp/pr257-profile-build.py child profile
python3 /tmp/pr257-profile-build.py child profile sanitized
python3 /tmp/pr257-profile-build.py child
cc -std=c11 -O2 -Wall -Wextra -Werror -I. tests/unit/test-xbox-vk-texture-binding.c -o /tmp/pr258-texture-binding-checkpoint
/tmp/pr258-texture-binding-checkpoint
```

Normal configured build:

```sh
meson test -C build test-xemu-tweaks-profile test-xemu-tweaks-config test-xbox-vk-texture-binding --print-errorlogs
```

This is focused local qualification using preexisting generated host headers/QEMU utility/TOML libraries, not a fresh full product build. Leak detection is disabled because of tracing; address/undefined checks remain active. Actual full CI must execute five profile TAP cases and seven binding cases. Historical native Windows product/tests remain evidence for the preceding implementation, not a newly built native product.

The formatter is diagnostic UI-thread metadata and may overlap renderer lifecycle publication. It is not a draw-transaction snapshot or full #94 artifact. Nonpersistent CLI overrides, executable/settings/workload identity, durable counters, ABBA/BAAB validator, nonzero real eligible/skip/reference reachability, Vulkan guest correctness, full XISO and matched Steam Deck performance remain unqualified. No native job/service upgrade, speedup claim or merge readiness is implied. Broader shader/PR260 runtime stays parked.

Read-only profile reviewer found no critical/important defect in the parent formatter, child registry integration, bounded output and status semantics. It reran the parent4/child5 profile binaries and both config/runtime binaries successfully. This review did not qualify full product/CI or native performance.
