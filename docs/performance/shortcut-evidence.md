# Advanced shortcut run evidence

This is opt-in measurement infrastructure for PR257/258 qualification. It does
not establish a Steam Deck baseline or a speedup. The common owner window and
offline comparison validator are implemented; a consumer must register its
actual counters. Until a supported owner supplies a complete window, the
artifact is explicitly incomplete. Native acceptance remains a separate gate.

## Command line

`-xemu-tweak KEY=VALUE` is repeatable and applies only to this process. Duplicate
keys use the last value. It never changes saved settings. Policy values are
`auto`, `disabled`, `enabled`. `vk_ubershader_mode` accepts `off`, `fallback`,
`prewarm`, `always`. `cache_shaders` is an independent policy. The derived
`vk_hybrid_ubershaders` permission cannot be overridden separately.

Request an artifact using all the run/window metadata:

```sh
xemu -config_path benchmark.toml \
  -xemu-tweak pgraph_bulk_packets=disabled \
  -xemu-shortcut-evidence run-A1.json \
  -xemu-shortcut-session comparison-A1 \
  -xemu-shortcut-workload fixed-workload-revision \
  -xemu-shortcut-input-sha256 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  -xemu-shortcut-order-group comparison \
  -xemu-shortcut-order ABBA \
  -xemu-shortcut-position 1 \
  -xemu-shortcut-start-frame 1200 \
  -xemu-shortcut-frames 3600
```

The hash/window above are illustrative. Use actual workload/input pins and a
measurement window appropriate to the native procedure. Metadata without an
output path, missing required values, invalid hashes/orders/positions, and
overflowing windows fail before SDL or worker startup. Other QEMU arguments are
preserved. Parsing failure leaves argv and caller outputs unchanged. Overrides
alone do not enable instrumentation or write an evidence file.

Output is written once on shutdown, including an early-exit fallback. Normal
shutdown waits for the worker to finish. Explicit write failure reports the path
and OS error and forces a failing process status. A successfully written file
may still describe an incomplete run. The configured window does not itself
terminate guest execution; the native procedure controls the run lifetime.

Evidence mode requires an explicit existing EEPROM and regular input files.
It resolves the first `-dvd_path` and one ordinary `-drive` HDD using QEMU's
legacy option parser, including escaped commas, without changing argv or saved
settings. The supported management arguments are `-qmp`, `-name`, `-msg`,
`-loadvm`, `-S`, `-no-shutdown` and `-no-reboot`. Other launch options, duplicate
resource routes and an invalid boot ROM fail before guest startup; they cannot
produce complete evidence for guessed inputs. Normal launches without evidence
retain their existing argument handling and default EEPROM behavior.

## Schema: `xemu-shortcut-evidence/v1`

| Fields | Meaning |
|---|---|
| `session_id`, `order_group`, `order`, `position` | Declared unique run and four-run comparison position. |
| `workload`, `input_sha256`, `input_provenance` | Runner declarations; xemu does not claim to verify guest input from these strings. |
| `commit`, `version`, `build_type`, `platform` | Build identity. Production build type/platform come from Meson, not a guessed debug flag. |
| `executable_path`, `executable_sha256` | Service-resolved running file, hashed once at initialization. On Linux `/proc/self/exe` retains the running inode if the launch pathname is replaced. |
| `requested_backend`, `requested_gpu` | Requested renderer and automatic/UUID/legacy selector. |
| `actual_backend`, `gpu`, `gpu_changed` | Owned successful-renderer publication. Vulkan name/UUID/driver UUID/vendor/device/API/driver/type are copied while their owner is valid. Missing identity is explicit. |
| `base_config_sha256` | Literal fingerprint of a private ConfigTree copy updated from effective startup configuration, including actual input paths. Obtaining it does not change the tree or persist process overrides. |
| `comparison_config_sha256`, `input_paths` | The comparison fingerprint replaces only the five bound system-input paths with a marker, retaining bound/unbound roles and every other setting. The owned path map retains the actual bootrom, flashrom, EEPROM, HDD and DVD paths; it does not attest their contents. |
| `settings`, `initial_profile` | Every registered tweak, actual ubershader mode and cache policy. Includes requested/selected/effective/available/reason/restart/origin and cache initialization eligibility. Startup profile is preserved separately. |
| `configured_window` | Requested owner-boundary start and count. A consumer must name its boundary; it is not automatically a guest flip or host presentation. |
| `actual_window`, `wall_interval_ns`, `wall_interval_source` | Observed published owner bounds and monotonic interval. The wall interval names its reference source; it is not isolated shader GPU time. Absent owner progress is not fabricated. |
| `counters`, `counter_units` | Checked sums of compatible same-name counters. Numbers are unsigned 64-bit JSON integers, including values above signed 64-bit range. |
| `sources` | Each source incarnation, retained counters/units, raw window, copied start/end profile and complete/overflow/retired status. |
| `execution`, `execution_revision` | Actual owner-published CPU route/hard FPU/MTTCG, voice converter/library/worker count, DSP engines/realtime, presentation transport/GL identity, observed vsync and surface scale. Missing components remain absent. Changes during or after a retained window invalidate admission rather than relabel its execution. |
| `complete`, `overflowed`, `collection_error`, `source_reset`, `profile_changed`, `profiles_published`, `windows_match`, `backend_match`, `invalid_counters` | Qualification limitations. Invalid sums are omitted while original per-source values remain available. A zero profile generation is an unpublished resolution, not an observed runtime profile. |

Cache fields describe permission and initialization, not a hit, accepted cache
artifact, cold state, or warmth. Cache contents/state still need native procedure
evidence. Missing queries, duration distributions and unsupported counters are
not substituted with zeroes. Native frame-tail metrics remain the tester's report.

## Ownership and bounds

The registry retains at most eight source incarnations and 64 descriptors per
source. Rejected registration/capacity is explicit and makes the artifact
incomplete. IDs, counter names and units are owned. Counter names are dotted;
names/IDs are limited to 128 bytes and units to 64 bytes. Retired sources remain
available, so teardown cannot erase earlier evidence.

Callbacks copy only an owner's protected published numeric block. Registry
locking protects callback lifetime; callbacks must not reenter the registry or
acquire PGRAPH locks. Owners release their publication lock before unregistering.
Unregister obtains the final copy before releasing the context. Snapshots own
their JSON data and survive later owner changes. Shutdown freezes the snapshot,
atomically replaces and durably flushes the output, and returns the same result
on repeated calls without more callbacks or writes.

Counter overflow, incompatible units, missing sources/GPU identity, incomplete
progress, changed profiles/backend, or source reset cannot become a complete
comparison. No hot-path allocations, formatting or registry operations belong
in a renderer adapter; that adapter must publish its numeric block at bounded
owner boundaries.

`XemuShortcutWindow` is a fresh zero-initialized renderer-owner object. It starts
at the exact configured boundary, counts only while active, and freezes at the
exact end. A skipped start, nonincreasing progress/time, changed profile or
execution revision, overflow, or teardown before the end is incomplete. The
callback copies its protected published block. No GPU pause, presentation
acknowledgment, input pacing, or disk write is introduced by this collector.

The APU observes actual DSP flags at its existing preference boundary, including
debug changes that leave saved preferences unchanged. An owner-local scalar
latch avoids allocation or registry work when the actual flags are unchanged.
Shutdown racing a late publication is a successful no-op; a publication cannot
abort normal early exit after the final evidence was frozen.

## Balanced comparison admission

`python3 scripts/validate-shortcut-comparison.py comparison.json` reads a
`xemu-shortcut-comparison/v1` manifest and eight evidence/runner sidecar pairs.
The order is ABBA then BAAB. `kind` is `setting_ab` (same executable, one policy)
or `code_patch_ab` (two explicitly declared executable/commit identities).
`policy_changes` and `effective_changes` give the requested and actual A/B
values; `execution_changes` names any intentional actual runtime difference.
`required_counters` names the positive counters proving target reachability.

Each `runs` entry supplies relative `evidence` and `runner` JSON paths. The
runner sidecar is extracted from the real canonical run records and contains:
unique `run_id`; the matched xemu `session_id`, `executable_sha256`, and
`input_sha256`; fixed `baseline_sha256`, `procedure_revision`, `catalog_sha256`,
and `guest_settings_sha256`; `cache_policy` and `start_policy`; exact ordered
`requested_tests` and `actual_tests`; `correctness`; and
`comparison_eligible`. Declaring a sidecar is not proof of guest input receipt.
Preserve its source run/report links in the owning PR evidence manifest.

The sidecar also requires `resource_bindings`, containing all five roles named
in `input_paths`. Each entry has `path`, `sha256` and `verified`: bound inputs
must match the reported path and have a verified content hash; unbound inputs
have an empty path, null hash and `verified: false`. Pin the actual starting
input contents using the canonical runner records, including private mutable
state prepared for the run. A template path or an assumed parent image is not
proof of that binding. Comparison requires identical verified starting contents
for each bound role across all eight runs. Literal configuration hashes may
differ for private per-run paths; the comparison hash must remain identical.
This permits isolated copies without ignoring changed input contents or other
configuration. Missing binding evidence rejects admission.

The validator rejects missing/unknown or malformed required execution fields,
GPU/driver identity, restart-pending policies, unbound/reused sidecars, wrong
guest records, incomplete/overflowed windows, missing units, and unexercised
target paths. It checks unsigned increasing owner timestamps and their wall
interval, plus immutable context and effective-state equality across repeats.
Duplicate JSON keys and files exceeding 4 MiB are rejected. It does not acquire
measurements or calculate improvement; its output always identifies the
performance verdict as `not_evaluated`. A passing admission still needs the
runner's per-test timing comparison and native correctness interpretation.

## Verification scope

Focused tests use the real parser, resolver, ConfigTree, QJSON and file writer.
They exercise supplied-string lifetime, `UINT64_MAX`, checked two-source sums,
overflow, unregister/export concurrency, retired data, capacity, conflicting
units, profile/backend/window mismatch, GPU changes, durable reopen and explicit
output failure. Config tests check determinism, unrelated setting changes and
exclusion of process overrides without saving or mutating global ConfigTree.
Private input paths change the literal hash while preserving the comparison
hash. Changed bound/unbound roles, unrelated settings, unverified or changed
input contents, and paths not bound to the artifact are rejected.

These tests use identified fixtures, not native game telemetry. Production
startup/PGRAPH/help sources must compile; full current-head CI and native
same-binary Vulkan ABBA/BAAB reachability/correctness remain acceptance gates.
Saving JSON alone does not qualify a performance claim.
