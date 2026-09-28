# Advanced requested/effective profile ownership

## Purpose and authority

Implement the remaining resolver/publication and reusable policy UI contract in
issue #93 on existing PR257; integrate it into dependent PR258. This supports
truthful Steam Deck A/B qualification. The supplied issue is the approved design
and implementation brief. No shader/capture/Asset Browser features belong here.

## Invariants

- Preserve the existing saved Boolean keys and defaults, including the newer
  accepted controls and ubershader mode migration. Auto is the first child policy
  default; serialized enum names remain lowercase.
- Enabled permits an eligible shortcut only; every existing renderer bounds,
  dirty-state, lifetime, capability, DMA and synchronization guard remains.
- Hot-path C access remains one lock-free aligned64-bit atomic load plus a mask.
  No allocations, strings, mutex or file I/O are added there.
- A worker mask contains effective permissions for the installed backend. A
  separate selected mask preserves process-start choices through unavailable
  backends, live edits, renderer reset and fallback.
- UI config is read only by the UI adapter. The pure resolver, renderer lifecycle
  publishers, status queries and formatter never read mutable g_config.
- The owned descriptive snapshot is copied under a low-frequency mutex. All rows
  in one returned snapshot/formatter invocation use one generation. A worker may
  observe the prior or next complete mask; this is not a per-draw transaction.
- Platform adapters are applied before the new effective mask/status publish.
- Do not relatch restart-only choices on renderer installation. Vulkan shader
  initialization continues using the existing atomic process mode accessor.

## Data and resolution

`XemuTweakRequestedState` contains one Auto/Disabled/Enabled policy per tweak and
an explicit requested Vulkan ubershader mode. The hybrid bit is derived from that
mode, preserving its existing meaning.

`XemuTweakEnvironment` holds actual renderer, Windows host eligibility, and Vulkan
ubershader installation/operational status. Successful GL initialization already
requires native S3TC; no invented capability is necessary for that route.

`XemuTweakResolution` owns renderer identity, selected/effective masks, complete
per-setting status, ubershader requested/process/active mode and publication
sequence. Reasons are static text. The pure resolver's sequence is zero; only
publication increments it. Auto follows named default policy and never enables
experimental issue149 or NV20 arithmetic by default.

The pure function takes requested/environment/startup values and an explicit
relatch flag. It returns a whole value and has no publication/platform effects.
Backend restrictions and restart latches are independent: losing Vulkan must not
lose a selected transient-growth choice or create a spurious restart warning.

## Publication and UI

Startup stores an owned requested value and startup selection before workers.
Live apply copies UI scalar values, resolves against the startup selection and
current environment, applies the wait adapter, then publishes mask and status.
Renderer publications update only owned environment and resolve the same request.
Queries return copies, never pointers into mutable config or publisher storage.

Status UI distinguishes Auto, explicit disable, effective permission,
unavailable reason and restart pending independently. A reusable policy combo
allows a restricted Auto/Disabled child policy; saving/browsing does not bypass
renderer guards. Existing Boolean controls keep their saved representation.

## Evidence and boundaries

Real regressions must fail for unpublished config edits and unavailable effective
worker bits. Pure tests cover defaults, live/restart, backend/platform/capability,
mode dependencies, invalid values and bit capacity. Concurrent publication tests
check the returned generation's internal consistency, not simultaneity with a
separately read mask. Existing actual generated config migration/persistence and
profile buffer tests remain required. Formatting, review and current-head CI are
required before describing the change as ready for human validation.

#94 nonpersistent CLI, durable session/counters/validator and native same-binary
ABBA/BAAB are subsequent performance contract work. Unit CI is not a Deck speedup.
Native UI/restart/gameplay/XISO qualification remains an explicit merge gate.
