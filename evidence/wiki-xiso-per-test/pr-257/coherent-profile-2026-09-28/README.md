# Owned requested/effective profile — 2026-09-28

Continues the supplied #93 design in existing PR257. A pure whole-profile resolver
retains process-start selections independently from backend-effective bits.
Low-frequency apply/lifecycle operations publish a mutex-owned status value and
one aligned 64-bit atomic worker mask; hot paths and renderer guards are unchanged.
No status/formatter/lifecycle operation reads mutable configuration. The UI adapter
copies scalar requests, and the platform wait adapter runs before publication.
The existing atomic process ubershader mode still supplies renderer initialization.

## Regression and verification

Baseline35bbc profile4 and actual generated config tests passed first. New runtime
regressions failed because an unpublished UI edit changed reported request and an
OpenGL session retained a Vulkan-only worker bit. Missing resolver/snapshot APIs
failed compilation (recorded as missing-feature compile results). After ownership
integration, five pure resolver and eight profile GLib/TAP cases pass strict
optimized GCC14.2 builds and ASan/UBSan. Actual generated defaults/persistence/
migration/live/restart tests pass both builds; legacy selection assertions are
retained against selected state and additionally check their effective worker bit.
The original incompatible selected-mask assertion failure is retained, not hidden.
Single-policy strict C11 test passes. Actual widgets/main-menu translation units
compile with optimized production flags and -Werror.

Profile tests cover retained copies/config allocation release and concurrent UI /
renderer publishers; each returned generation's masks/states/stages must agree.
They do not prove simultaneity with a separately read hot-path mask. Resolver tests
cover default Auto and explicit policy, platform/backend restrictions, unavailable
restart selections, fresh relatch, same-Boolean mode changes, capability/dependency
failure and invalid requests. Existing buffer/complete-key tests remain.

Local driver /tmp/pr257-profile-build.py regenerates current config, compiles actual
changed sources and reuses existing QEMU utility/TOML/host dependencies. Commands
are retained in logs. This is not a fresh full product/native/Steam Deck campaign.
Address/UB sanitizers run with leak checking disabled under tracing; no leak result
is claimed. Full current-head CI and human native UI/restart/gameplay validation
remain gates. #94 nonpersistent overrides, durable session/counters and paired-run
qualification remain required. No performance gain is claimed.
