# Requested/effective profile follow-up — 2026-09-28

Implements the low-frequency formatter requested in #93 on existing PR257. All registered tweak keys, policy/effective permission, backend/platform/capability/dependency reason and restart status are present; the active/requested ubershader mode is separate from its derived Boolean permission. A single captured mask/backend/mode serves every output row. Publication may overlap renderer lifecycle changes; this is not an atomic draw-state snapshot. The full coherent requested/status publication, generic typed controls and #94 session/CLI contract remain future acceptance work.

The profile is UI-thread-only. It changes neither saved settings nor the hot-path mask/guards. It returns snprintf-style required length and terminates retained prefixes. No file I/O or per-draw instrumentation is added. `perf.cache_shaders` is not registered as a tweak bit; its renderer-latched eligibility belongs in full #94 session export rather than a guessed effective Boolean.

## Verification

The baseline production config/runtime tests passed before editing. New formatter tests first failed compilation because the API was absent (adjacent missing-api.log); this is a missing-feature compile result, not a claimed runtime red. Strict optimized GCC14.2/C++17 compilation of the actual new formatter and freshly generated current configuration passes four GLib/TAP cases. ASan/UBSan passes the same four cases. They cover live edits versus pending restarts, complete keys/unsupported backend, actual degraded Vulkan status, unchanged active configuration, and zero/one/short/exact/full capacities with guard bytes. Existing defaults/persistence/migration/live/restart tests also pass after the change. Leak detection is disabled due tracing; no leak result is claimed. New source clang-format and diff whitespace checks pass.

Focused local driver `/tmp/pr257-profile-build.py` repoints the existing Meson template and QEMU utility/TOML dependencies from `/home/codex/pr238-source/build`. It regenerates xemu-config.h from this worktree, compiles the real changed sources and runs the executable. This is not a fresh full product build or native Windows/Steam Deck qualification. Exact committed test/production source hashes are adjacent. Commands:

```sh
python3 /tmp/pr257-profile-build.py profile
python3 /tmp/pr257-profile-build.py profile sanitized
python3 /tmp/pr257-profile-build.py
```

Normal build reproduction:

```sh
meson test -C build test-xemu-tweaks-profile test-xemu-tweaks-config --print-errorlogs
```

The new target is registered as TAP and must show four executed passing cases in actual full unit CI. The older Windows product/native evidence in the PR belongs to3d202f9160, not a binary built with this formatter. No performance gain or real workload reachability is established. The existing PR258 needs this parent plus a profile registry entry/regression for its typed clean-stage policy.
