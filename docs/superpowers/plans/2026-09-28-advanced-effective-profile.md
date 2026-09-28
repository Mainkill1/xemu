# Advanced effective profile implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan inline. Steps use checkbox syntax for tracking.

**Goal:** Complete #93 whole-profile resolution, owned coherent publication and reusable typed UI, then integrate the existing #258 child.

**Architecture:** A pure resolver returns selected/effective masks and owned statuses. A low-frequency mutex protects requested/startup/environment/publication state; rendering retains one atomic mask read. UI and formatter copy one published generation.

**Tech Stack:** C11/C++17, QEMU aligned64-bit atomics, GLib mutex/TAP, generated TOML config and existing ImGui widgets.

**Spec:** `docs/superpowers/specs/2026-09-28-advanced-effective-profile.md`, supplied issue #93.

## Global Constraints

- Preserve existing saved keys/defaults and all renderer correctness guards.
- No per-draw mutex, allocation, formatting or file I/O.
- No new PR, merge, release, tester upgrade or broader shader/Asset Browser work.
- Existing isolated branch `feature/issue93-policy-foundation` is the implementation workspace; dependent `feature/issue95-clean-stage-policy` receives integration.
- Full #94/native qualification remains required; do not redefine merge readiness around units.

## Review Focus

- Renderer reset/fallback must retain latched selections without false restart warnings.
- Renderer initialization must consume the original process ubershader mode before effective backend bits are available.
- Unpublished UI edits and released config allocations must not change or invalidate the snapshot.
- Concurrent UI/lifecycle publication must produce internally consistent status generations with static reasons.
- Child Auto/Disabled persistence must retain unrelated settings and suppress only its intended eligible shortcut.

## Task1: Pure whole-profile resolver

**Files:** `ui/xemu-tweaks.h`, new `ui/xemu-tweak-resolver.c`, `ui/meson.build`, `tests/unit/meson.build`, new `tests/unit/test-xemu-tweaks-resolver.c`.

**Interfaces:** Add `XemuTweakRequestedState`, `XemuTweakEnvironment`, `XemuTweakResolution`; `xemu_tweaks_resolve(const XemuTweakRequestedState *, const XemuTweakEnvironment *, const XemuTweakResolution *, bool)` returns a value. Resolution includes selected/effective masks, statuses, modes and sequence.

- [x] Write GLib/TAP tests for default Auto, explicit modes, unsupported platform/backend, live apply, independent restart selections across fallback, ubershader capabilities/dependencies and invalid values.
- [x] Compile/run tests against baseline; expected missing resolver API (record as missing-feature compile result).
- [x] Implement pure resolution and register source/tests. Named Auto policy follows existing recommended defaults; issue149/NV20 remain off. Hybrid is derived from requested/process mode.
- [x] Run strict optimized test and actual existing single-policy unit; expected all cases pass with no config/platform side effects.
- [ ] Commit resolver and its exact red/green evidence.

## Task2: Owned publication and effective mask

**Files:** `ui/xemu-tweaks.c`, `ui/xemu-tweaks.h`, `tests/unit/test-xemu-tweaks-profile.cc`, `tests/unit/test-xemu-tweaks-config.cc`, `docs/performance/tweaks.md`.

**Interfaces:** `XemuTweakResolution xemu_tweaks_snapshot(void)` copies the low-frequency published value. Existing APIs retain signatures. All config reads live only in the apply adapter; startup selected values and mode are retained separately from backend-effective bits.

- [x] Add unpublished-config and unavailable-mask profile regressions; owned-config assertion fails on baseline35bbc (retained /tmp/pr257-coherent-profile-red.log).
- [x] Independently run unavailable-mask regression; expected unsupported Vulkan bit wrongly remains enabled on OpenGL.
- [x] Add coherent returned-generation/concurrent lifecycle and retained snapshot tests.
- [x] Replace split runtime atomics/status reconstruction with one mutex-owned request/startup/environment/resolution. Apply the wait adapter before one atomic effective-mask store; expose process mode through existing atomic accessor.
- [x] Adapt old selected-permission assertions explicitly to selected snapshot values, retaining saved-default/migration checks and adding installed-renderer effective-bit checks. Do not weaken any bounds/persistence assertion.
- [x] Run strict and ASan/UBSan profile/config/resolver cases; expected all pass. Leak checking remains unavailable under tracing and must be labeled.
- [ ] Document generation, initialization and adapter ownership; commit with full red/green logs and source hashes.

## Task3: Shared UI and dependent child integration

**Files:** `ui/xui/widgets.hh`, `ui/xui/widgets.cc`, `ui/xui/main-menu.cc`; dependent PR258 registry/resolver/config/profile tests and current clean-stage menu row.

**Interfaces:** `PerformancePolicyCombo(label, int *requested, XemuTweak, help, bool allow_enabled)` applies/saves edits then renders status; Boolean controls reuse `DrawTweakEffectiveStatus(XemuTweak)`.

- [ ] Add Auto/Disabled child snapshot regression, including default/serialized lowercase/unrelated settings/restart/backend transitions. Watch missing child resolver registration fail.
- [ ] Implement shared status UI and generic restricted policy selector; integrate parent into child without touching renderer guards.
- [ ] Run parent/child strict/sanitized tests, actual config migration, seven texture-binding cases, and compile affected UI using existing full-product build templates.
- [ ] Format new files, check focused existing-file changes and whitespace, and request one fresh whole-branch review.
- [ ] Address important review findings with regression tests, commit/publish to existing PRs and observe exact-head CI. Update PR/checkpoint remaining native/#94 gates.

## Verification commands

Local focused driver uses freshly generated current config and existing host dependencies (not a fresh full product):
`python3 /tmp/pr257-profile-build.py profile`, `python3 /tmp/pr257-profile-build.py`, corresponding `sanitized` and `child` options. Extend it for resolver source/test before use.

Normal Meson targets: `test-xemu-tweaks-resolver`, `test-xemu-tweaks-profile`, `test-xemu-tweaks-config`, `test-xemu-tweak-policy`, `test-xbox-vk-texture-binding`, `test-xbox-vk-device-selection`, `test-xbox-vk-spirv-prewarm`.

Actual CI must execute TAP subtests. Native manual gates remain those in issue93; do not launch normal tests through remote shell. Shared tester upgrade permission is still pending.

## Execution ledger

- Existing isolated worktree verified;35bbc baseline profile4/config tests pass.
- Supplied issue93 design and user authorization to finish existing PRs govern implementation; execute inline without a redundant permission cycle.
- Ruling: selected and effective masks are separated explicitly, replacing prior selected-mask semantics. Backend-unavailable worker bits become false as required by93; all existing in-backend correctness guards and saved defaults remain. Cost if wrong: an initialization consumer could need selected state, so its call sites require review/native qualification.

- Tasks1/2 local verification: strict and ASan/UBSan5resolver/8profile plus generated config pass; actual widgets/menu compile. Commit is combined to keep the complete resolver/publication transition atomic for the dependent child.
