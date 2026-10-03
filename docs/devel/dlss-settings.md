# DLSS settings: setup and implementation handoff

Tracking: [PR #262](https://github.com/Mainkill1/xemu/pull/262),
[issue #261](https://github.com/Mainkill1/xemu/issues/261).

## What this branch implements

Open **Settings > DLSS**, immediately after Display. Configuration, runtime
status and setup help are on one page. The page is available without a DLL or
NVIDIA SDK and on non-Windows builds; external adapters currently run only on
Windows with the Vulkan renderer. Nothing is downloaded by opening the page.

**This is a presentation scaffold, not a working Neural Rendering backend.**
The included pass-through and GPU-copy adapters validate integration. The
Streamline outline does not evaluate NR. A selected mode or a loaded library
is not evidence of working DLSS, and RTX 3090 NR remains unverified.

## First run: use the menu, not environment variables

1. Build xemu normally from this branch with Vulkan enabled. From a Visual
   Studio developer PowerShell, build the separate reference adapters:

   ```powershell
   .\contrib\neural-present\build-reference-adapters.ps1 -Configuration RelWithDebInfo
   ```

2. Open Settings > DLSS. Set **Adapter Validation**, choose **Custom DLL**,
   and browse to the built `xemu-neural-present-pass-through.dll`. Select
   Vulkan in Display or use **Switch renderer to Vulkan** on the DLSS page.
3. Exit and restart xemu. Resetting the Xbox is not sufficient. Load a title
   and inspect runtime status. Expect pass-through, with **NR frames = 0**.
4. Repeat using `xemu-neural-present-vulkan-copy.dll` under Vulkan validation.
   Compare captures against Off, and test resizing, teardown and both shared
   OpenGL and host-copy presentation. Expect pixel-identical output; this must
   be demonstrated on a GPU, not inferred from a successful build.
5. Use Neural Rendering mode only when testing an actual NR adapter. The
   Streamline outline will still bypass evaluation; its presence does not
   complete this step.

For Auto-detect, place the intended adapter next to xemu under the single name
`xemu-neural-present.dll`. Keep the adapter's required dependencies in its
proper runtime directory. Auto-detect never searches arbitrary directories,
loads every DLL, downloads a model, or selects another DLL after a failure.
Custom paths must be absolute. Clearing a custom path leaves a visible invalid
configuration; it does not silently switch to a different adapter.

## Configuration contract

```toml
[display.dlss]
mode = 'off'                        # off | validation | neural_rendering
adapter_selection = 'auto'          # auto | custom
adapter_path = ''
processing_resolution = 'auto'      # auto | 720p | 1080p | display
```

All four settings are persisted through xemu's generated configuration. Missing
keys preserve Off/Auto defaults. The menu saves edits immediately. Mode, adapter
and processing selection are startup-latched; a renderer recreation must not
relatch them. The page distinguishes the saved mode, startup mode and actually
produced feature. Existing saved menu positions migrate once using
`general.settings_menu_version`; Snapshots/System/About shortcuts use named IDs.

Automatic and 720p request an aspect-preserving extent bounded by 1280x720;
1080p uses a 1920x1080 bound. These presets never upscale a smaller display
image. Display uses the completed xemu display image, not the OS window size.
For example, a 2560x1920 input with the 720p bound requests 960x720.

**Extent selection requests work size; it does not implement resampling.**
A real adapter must allocate/convert its private inputs and output at the
correct sizes. The copy adapter still copies at display size. The status label
therefore says *Requested processing extent*.

## Developer overrides

| Variable | Meaning |
| --- | --- |
| `XEMU_EXPERIMENTAL_NEURAL_PRESENT` | Explicit startup Boolean override. True preserves Validation when selected; otherwise an Off request becomes NR. False forces Off. |
| `XEMU_NEURAL_PRESENT_PLUGIN` | Exact absolute DLL path, overriding both Auto and Custom. |
| `XEMU_NEURAL_PRESENT_WORK_WIDTH` / `XEMU_NEURAL_PRESENT_WORK_HEIGHT` | Both 1..8192 for an exact developer extent, or both 0 for display size. |

Environment values are read once after config load. They never overwrite saved
preferences. The page identifies which fields are overridden; remove the
variables from the parent launch environment and restart to return to saved
settings. Invalid Boolean or dimension values are rejected with a reason.
Changing a saved field hidden by an override does not falsely change the
startup snapshot or demand a restart for an unchanged effective request.

## Status and thread ownership

`ui/xemu-dlss.cc` owns the startup request, retained environment and copied
runtime snapshot. Only the settings/UI adapter reads `g_config`; Vulkan reads
an owned startup copy. No UI widget loads a DLL, calls Streamline, accesses a
Vulkan object or holds a renderer-owned string pointer.

A generation-tagged publisher prevents an old renderer from overwriting the
new renderer's status. A mutex protects the copied descriptive snapshot, never
a draw or an SDK callback. Normal counter publication is throttled to four
updates per second; state/feature transitions publish immediately. Publication
is not atomic with the next guest draw, and counters may lag by a quarter second.

Runtime facts include adapter identity, actual selected Vulkan GPU, requested
extent, output feature, original-output bypasses, failures and NR frames.
Processed counts successful adapter frame callbacks, including pass-through;
bypassed includes both host rejections and adapter pass-through. These counters
are not mutually exclusive and are not a presented-FPS measurement. NR frames
increase only for a validated NR output after the existing display fence.
Zero GPU time is shown as **not reported**, never as zero inference cost.
The timing is adapter-reported, not a new xemu GPU timing implementation.

Validation mode admits only `XEMU_NEURAL_PLUGIN_CAP_VALIDATION_ONLY` adapters
before bootstrap, then rejects non-pass-through output. Both reference adapters
declare this additive ABI capability. It is a contractual guard for trusted
adapters, not a sandbox for arbitrary native DLLs. The ABI remains version 2.

## File map and remaining backend work

| Area | Files / responsibility |
| --- | --- |
| Menu | `ui/xui/main-menu-dlss.cc`, `main-menu.hh`, narrow tab/shortcut changes in `main-menu.cc` |
| Persistence | `config_spec.yml`, `ui/xemu-dlss-config.hh`, `ui/xemu-settings.cc`, `ui/xemu-settings-menu.h` |
| Owned request and runtime status | `ui/xemu-dlss.h`, `ui/xemu-dlss.cc` |
| Renderer producer | `hw/xbox/nv2a/pgraph/vk/neural-present-vk.c` |
| Adapter contract | `hw/xbox/nv2a/pgraph/neural-present-plugin.h` |
| Tests | `tests/unit/test-xemu-dlss.cc`, `test-xemu-dlss-config.cc`; existing neural policy/ABI/loader tests |

Implement the real backend in this order:

1. Build and run the menu/configuration integration, including feature-off and
   non-Windows configurations. Qualify the reference copy path on Windows.
2. Resolve Vulkan creation-proxy ownership, failure handling and SDK/module
   shutdown order. The inherited scaffold can unload a proxy while Vulkan
   objects still exist; the settings work does not establish lifecycle safety
   for a production proxy adapter. Keep this a merge gate.
3. Implement private original-color/history/output resources and correct
   resampling. Never write guest-visible render targets. A capability flag
   cannot roll back already-recorded GPU commands or a device failure.
4. Implement motion input and its synchronization using the optical-flow guide.
   No validated depth or motion resource is currently supplied by the host.
5. Use the matching NR runtime's documented options/tag/evaluate contract.
   Resolve the missing conventional Vulkan swapchain lifecycle in xemu's
   Vulkan-to-OpenGL presentation path; do not guess private SDK entry points.
6. Qualify real NR output and temporal resets on the actual RTX 3090, with exact
   driver/runtime hashes, at least 300 evaluations, original/enhanced captures,
   resize, pause/load/reset/shutdown tests and frame-time/VRAM measurements.

### External libraries and references

Use the existing [adapter guide](../../contrib/neural-present/README.md),
[renderer integration design](dlss5-neural-rendering.md) and
[NVOFA Vulkan synchronization guide](../../contrib/neural-present/nvofa-vulkan-integration.md).
The scaffold references [NVIDIA Streamline](https://github.com/NVIDIA-RTX/Streamline),
[NVIDIA Optical Flow SDK](https://developer.nvidia.com/opticalflow-sdk), its
[programming guide](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html),
and the [Vulkan SDK](https://vulkan.lunarg.com/). Pin the actual package versions
and verify their APIs before completing the outline. No vendor runtime or model
is bundled, and no installed SDK/version is claimed by this settings change.

## Repeatable checks

SDK-free configuration/status, reference-loader and adapter build:

```sh
cmake -S contrib/neural-present -B build-neural-present -DCMAKE_BUILD_TYPE=Debug
cmake --build build-neural-present
ctest --test-dir build-neural-present --output-on-failure
```

The settings target compiles the real resolver/publication source without
ImGui, QEMU, Vulkan or NVIDIA headers. It tests path/override validation,
startup ownership, restart semantics, extent policy, menu migration, validation
mode, truthful feature publication and concurrent snapshots. Vulkan-copy is
skipped explicitly when the Vulkan SDK is absent.

In a configured xemu Meson build, run `test-xemu-dlss` and
`test-xemu-dlss-config` plus the existing neural state/ABI tests. The generated
config test exercises real CNode defaults, TOML roundtrip and preservation of
unrelated settings; it is separate from the SDK-free resolver test.

Manual UI acceptance: keyboard/controller navigation reaches DLSS after Display;
old saved tabs and direct shortcuts remain correct; mode/path/resolution survive
restart; rejected DLL paths explain why; environment overrides are visible;
no backend is shown active after a failed load or teardown; reference adapters
never show NR. Check small windows and both shared/host-copy presentation.

This branch remains draft until full product builds, native UI execution,
Vulkan validation and the outstanding NR/runtime gates are recorded. Keep
hardware/performance evidence on this emulator PR/issue, not evidence-only PRs
in `xemu-perf-tests`. Do not change the fixed baseline for this feature.
