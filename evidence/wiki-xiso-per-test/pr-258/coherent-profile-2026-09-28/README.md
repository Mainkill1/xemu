# Coherent clean-stage policy integration — 2026-09-28

Integrates PR257's owned complete resolution/publication into existing PR258.
The child registers its existing typed Auto/Disabled value in the UI adapter and
recommended default, uses the shared policy/status widget with two choices, and
preserves all renderer guards/counters. An exact diff against e2cb051a0b7c shows
no hw/xbox changes. Parent startup-selected choices remain separate from actual
backend-effective permissions.

## Verification

The new child pure regression first failed when Auto was omitted from recommended
resolution. The real generated-config profile then failed when the typed child
request was omitted from the adapter. Both runtime failures are retained. After
registration, six resolver and nine profile GLib/TAP cases pass strict optimized
and ASan/UBSan builds. Actual generated config/default/persistence/migration cases
also pass; the child saved lowercase disabled fixture now confirms unrelated
cache_shaders and pgraph_bulk_packets settings survive save/reload. The shared
status model reports Auto/Disabled and effective/restart/backend independently.

Actual widgets and main-menu translation units compile with strict production
flags; related texture-binding, device-selection and SPIR-V prewarm tests are
compiled from this checkout and run. Full commands/output are retained. Seven
binding subtests execute, including the policy gate and existing transition,
source-identity and retry controls.

Local builds use existing host/QEMU dependencies and freshly generated child
configuration, not a full rebuilt native application. Address/UB sanitizers run
with leak checking disabled under tracing; no leak proof. Native UI/restart,
current-head CI, #94 CLI/durable session/validator and same-executable ABBA/BAAB
with nonzero eligible/skip/reference counts remain gates. No speedup is claimed.
