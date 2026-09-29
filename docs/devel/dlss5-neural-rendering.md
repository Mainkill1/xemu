# Experimental DLSS 5 Neural Rendering presentation scaffold

Tracking issue: [#261](https://github.com/Mainkill1/xemu/issues/261).
Reviewed base: `2cbabc7152866dd19fb2c6279c776019f3f5df9a`.

## Status and scope

**Draft scaffold, not a working DLSS NR implementation.** This branch moves the
previous local source package into version control for review and completion.
No full xemu build, GPU execution, or RTX 3090 Neural Rendering result is claimed.
The included Streamline outline deliberately bypasses frames; the reference
adapters only pass through or copy pixels.

The original downloadable unified patch was constructed against reduced source
fragments and is not the canonical patch for this repository. Use this branch's
actual diff. Standalone tests do not establish correctness of the full renderer.

The experimental goal remains genuine neural-enhanced presentation on Windows
x64 / Vulkan / RTX 3090. The NVIDIA support matrix and the exact runtime must be
checked separately; a public feature identifier or a successful DLAA evaluate
is not proof that NR works on Ampere.

## Architecture

```text
Original NV2A rendering / guest-visible resources (unchanged)
    |
    v
Final host-only Vulkan display image
    |
    +-- off / unsupported / invalid inputs --> normal presentation
    |
    v
Optional adapter: prepare, record, complete
    |
    v
Finished host presentation image in the required layout
    |
    v
Existing Vulkan-to-OpenGL shared image or host-copy path
    |
    v
xemu interface and window
```

The intended insertion point is after the final display render pass ends and
before its presentation transitions/copy. The adapter receives the host display
image, not guest VRAM or a guest render target. Shader/draw capture tools are for
input investigation, not a requirement to capture all geometry every frame.
The game's HUD can already be baked into color; excluding xemu's interface is
not universal in-game HUD separation.

## Source map

| File/area | Responsibility |
| --- | --- |
| `hw/xbox/nv2a/pgraph/neural-present-plugin.h` | SDK-neutral C ABI, bootstrap requirements, create proxies, output validation. |
| `hw/xbox/nv2a/pgraph/neural-present-loader.c` | Load/unload and interface validation. |
| `hw/xbox/nv2a/pgraph/neural-present-state.h` | Frame policy, history epochs, bypass reasons, result state. |
| `hw/xbox/nv2a/pgraph/vk/neural-present-vk.c` | Vulkan controller, environment settings, lifecycle and frame dispatch. |
| `vk/display.c`, `vk/renderer.c`, `vk/renderer.h`, `vk/instance.c` | Narrow integration points for presentation and device creation. |
| `contrib/neural-present` | Reference adapters, build/launch scripts, SDK outline and motion design. |
| `tests/unit/test-xbox-neural-present-*` | Portable policy/ABI tests and dynamic-loader smoke test. |

The ABI is version 2; the structure names retain the scaffold's `V1` suffix.
They are an experimental project interface, not NVIDIA types. An adapter with
an incompatible ABI is rejected rather than called as though it were compatible.

## Build and use

See [adapter instructions](../../contrib/neural-present/README.md). The default
xemu path does not load an adapter. Environment variables opt into the draft:

| Variable | Purpose |
| --- | --- |
| `XEMU_EXPERIMENTAL_NEURAL_PRESENT=1` | Enable discovery. |
| `XEMU_NEURAL_PRESENT_PLUGIN` | Absolute adapter path. |
| `XEMU_NEURAL_PRESENT_WORK_WIDTH` / `WORK_HEIGHT` | Paired processing extent; omit both for display extent. |

The full names of the last pair are `XEMU_NEURAL_PRESENT_WORK_WIDTH` and
`XEMU_NEURAL_PRESENT_WORK_HEIGHT`. Processing extent is separate from the final
window extent. The first version has no normal UI controls or hot reload.

## Frame policy and its current limits

The state key contains surface lifetime, draw time, output generation, guest
frame time, scanout address, display/work extents, surface scale and configuration
generation. Existing display reuse is retained. Standalone tests cover first
frame, exact duplicates, new generations, extent/configuration changes, backwards
guest time, bypass, failure latching and reset.

This is not a completed universal source-frame identity scheme. Qualify it with
CPU-written surfaces, alternating render targets, overlays, scanout line offsets,
interlacing, pause/resume, save-state loads and shader replacement changes. The
reset API does not by itself prove every lifecycle event is wired. In particular,
complete explicit save/load/title/flush reset coverage before temporal inference.

PVIDEO and interlaced output are initially bypassed. The wrapper supplies no
validated scene depth and no host motion resource. An adapter declaring required
depth is bypassed; an internal-motion capability must actually be implemented
before its output is used for neural evaluation.

## GPU ownership and failures

`prepare_frame` may allocate extent-dependent private resources. `record_vulkan`
records work into xemu's supplied command buffer. `complete` consumes the result
after the existing display submission completes. The first path relies on the
existing synchronous display behavior; this is not an asynchronous pipeline.

The adapter must restore the required image layout, retain allocations while
referenced, and obey queue synchronization. Creation proxies own any requested
extra extension/feature/queue setup; the wrapper validates declared requirements
but does not merge arbitrary Vulkan feature chains itself.

`TRANSACTIONAL_IN_PLACE` is an adapter contract, not a guarantee enforced by a
GPU rollback mechanism. Production NR must copy original input into private
storage, evaluate into a different image, and only publish completed output.
After commands are recorded or a device is lost, returning an error cannot undo
GPU work. Test failure and teardown before enabling external runtimes for users.

The scaffold latches three consecutive retryable failures, or an immediate fatal
failure. Audit SDK/proxy unload ordering carefully: runtime lifetime can outlive
a frame error when created devices or function pointers still depend on it.
Do not treat the draft's fallback paths as validated driver recovery.

## Required external integration

### Streamline / NR runtime

The supplied outline uses the reviewed public Streamline 2.14.1 API surface:
load the signed interposer, initialize with this integration's project identity,
disable OTA for reproducibility, request NR, query requirements before Vulkan
creation, use creation proxies, and query the actual physical adapter.

The reviewed headers name `sl::kFeatureDLSS_NR` and uplift color tags; they do
not supply the complete NR options/evaluate contract used by this proposal.
Obtain the matching SDK/runtime contract. Do not invent options structures or
consider SR/DLAA output equivalent to NR.

Complete the backend in this order:

1. Prove genuine NR create/evaluate on the actual 3090 in an independent probe.
   Record driver, OS, adapter identity, runtime hashes and feature counters.
2. Query accepted formats, extents, color encoding, required inputs and reset
   semantics. Allocate distinct original-input, output and history resources.
3. Supply correct motion (and depth/masks only when required and valid), obtain a
   frame token, set exact tags/options, evaluate NR and publish completed output.
4. Resolve Streamline's common presentation lifecycle. Xemu has no Vulkan
   swapchain in this path; use documented evaluate-only support or a validated
   bridge rather than guessed private functions.
5. Retire all SDK and GPU work before resource/device/runtime destruction.

The outline contains TODOs for these stages; defining a preprocessor symbol is
not an implementation. Keep it bypassed until the actual backend exists.

### Motion provider

The proposed initial implementation uses NVIDIA Optical Flow SDK 5.0 over two
consecutive original images. Its native Vulkan execution requires a separate
queue/semaphore handoff, not a CPU call hidden midway through an unsubmitted
command buffer. See [NVOFA design](../../contrib/neural-present/nvofa-vulkan-integration.md).
A compute-based optical-flow prototype is an alternative; neither is supplied
as working code in this draft.

Geometry-derived motion can be added later using verified current/previous draw
correspondence. Shader hashes and reused buffer addresses are not unique object
identities. Universal model/material reconstruction is not a first prerequisite.

## Verification and merge gates

Portable code tests can be run without proprietary SDKs:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. \
  tests/unit/test-xbox-neural-present-state.c -o /tmp/neural-state
/tmp/neural-state
cc -std=c11 -Wall -Wextra -Werror -I. \
  tests/unit/test-xbox-neural-present-plugin-abi.c -o /tmp/neural-abi
/tmp/neural-abi
cmake -S contrib/neural-present -B build-neural-adapters -DBUILD_TESTING=ON
cmake --build build-neural-adapters
ctest --test-dir build-neural-adapters --output-on-failure
```

Before calling this ready:

- [ ] Full xemu builds/tests on supported platforms, disabled path included.
- [ ] Actual Windows build of reference adapters and SDK outline.
- [ ] Vulkan-copy pixel identity on shared-GL and host-copy presentation.
- [ ] Vulkan validation: barriers, layouts, frame reuse, resize, teardown.
- [ ] Complete reset coverage and safe runtime/proxy lifetimes on failures.
- [ ] Motion provider with known-vector tests and disocclusion diagnostics.
- [ ] Genuine NR tagging/evaluate and non-swapchain presentation lifecycle.
- [ ] Actual 3090: 300 consecutive NR evaluations, on/off captures and resets.
- [ ] Timings, frame-time distribution, VRAM and disabled-feature overhead.

Keep correctness captures of original emulation separate from enhanced output.
Evidence belongs on this emulator issue/code PR, not evidence-only PRs in
`xemu-perf-tests`. Do not change the fixed test baseline as part of this feature.

## References

- [NVIDIA DLSS feature/hardware matrix](https://www.nvidia.com/en-us/geforce/technologies/dlss/)
- [NR overview](https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/)
- [Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1)
- [Streamline manual Vulkan hooking](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideManualHooking.md)
- [Streamline Vulkan helpers](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/include/sl_helpers_vk.h)
- [Optical Flow SDK](https://developer.nvidia.com/opticalflow-sdk)
- [NVOFA programming guide](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html)
- [Vulkan SDK](https://vulkan.lunarg.com/)

These links identify the libraries and contracts to investigate. The checked-in
code does not bundle proprietary DLLs or models, and source review alone is not
hardware qualification.
