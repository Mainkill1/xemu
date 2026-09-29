# xemu experimental neural-presentation adapters

Draft implementation for [feature #261](https://github.com/Mainkill1/xemu/issues/261).
The source is checked into this branch; no overlay installer is needed.
See [the integration guide](../../docs/devel/dlss5-neural-rendering.md).

**None of the included adapters performs DLSS Neural Rendering.** The
pass-through adapter exercises the loader, the Vulkan-copy adapter is an
unvalidated GPU insertion/lifetime test, and the Streamline outline deliberately
bypasses evaluation. Do not treat this draft as RTX 3090 support or merge-ready.

## Included code

- Versioned C ABI in `hw/xbox/nv2a/pgraph/neural-present-plugin.h`.
- Windows runtime loader, with an absolute path required by the renderer wrapper.
- Pre-device bootstrap/create-proxy contract for Vulkan requirements.
- Frame/history policy, failure latching, and distinct produced-feature values.
- Pass-through and GPU-copy reference adapters with a standalone CMake build.
- Streamline SDK-facing bootstrap/support-query outline, launch script, and
  runtime manifest collector.

The Vulkan copy adapter records `display -> private image -> display`. It is
intended to verify the insertion point; pixel identity, Vulkan validation,
resize, and both presentation transports still need execution on real hardware.

## Build reference adapters

From the repository root, on Windows with CMake and a C compiler:

```powershell
.\contrib\neural-present\build-reference-adapters.ps1
```

For the portable loader smoke test on Linux/macOS:

```sh
./contrib/neural-present/build-reference-adapters.sh
```

CMake skips the Vulkan-copy target when the Vulkan SDK is missing. To build the
optional Streamline outline on Windows, supply the matching SDK:

```powershell
.\contrib\neural-present\build-reference-adapters.ps1 `
  -StreamlineSdkRoot C:\SDK\streamline-sdk-v2.14.1
```

The Streamline target is an outline, not a working evaluation backend. The
Windows build itself remains a validation gate; standalone Linux smoke tests
are not evidence that the full xemu or Streamline targets build.

## Run

Use PowerShell 7 on Windows for the helper scripts. Launch a built Vulkan xemu
with the pass-through DLL first, then the copy DLL under Vulkan validation:

```powershell
.\contrib\neural-present\launch-experimental.ps1 `
  -XemuPath C:\src\xemu\build\qemu-system-i386.exe `
  -AdapterPath C:\adapters\xemu-neural-present-vulkan-copy.dll `
  -LogPath C:\logs\xemu-neural-present.log
```

Manual configuration is equivalent:

```powershell
$env:XEMU_EXPERIMENTAL_NEURAL_PRESENT = '1'
$env:XEMU_NEURAL_PRESENT_PLUGIN = 'C:\adapters\xemu-neural-present-vulkan-copy.dll'
# Optional paired neural work extent; not the window size:
$env:XEMU_NEURAL_PRESENT_WORK_WIDTH = '1280'
$env:XEMU_NEURAL_PRESENT_WORK_HEIGHT = '720'
.\xemu.exe
```

Disabled is the default. No normal settings-page DLSS toggle is implemented.
The initial adapter has no host-supplied motion vector buffer or validated depth
association. PVIDEO and interlaced output bypass the adapter.

## Adapter contract

Export `xemu_neural_present_get_api`, accepting ABI version 2. Bootstrap before
Vulkan creation and retain requirement strings until shutdown. Supply both
creation proxies when additional extensions, feature bits, or queues are
required; the host does not implement a general requirement-merging engine.

Use only the host presentation image. Record into the supplied command buffer,
return to the specified layout, and retain private resources until all users
retire. The transactional capability is a promise the adapter must honor, not a
sandbox or a host rollback mechanism. A broken adapter can still corrupt state.
The production NR implementation must keep original color and private output
separate until successful evaluation, then copy the finished result back.

Report pass-through, DLAA, Super Resolution, and NR distinctly. Validation of
flags can reject inconsistent claims; actual NR execution must additionally be
proved with runtime counters, captures, and hardware evidence.

## External libraries and references

### NVIDIA Streamline

The reviewed reference is Streamline 2.14.1. Pin the actual package used rather
than building against an unbounded "latest":

- [Official source](https://github.com/NVIDIA-RTX/Streamline)
- [2.14.1 release](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1)
- [Manual Vulkan hooking](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideManualHooking.md)
- [Vulkan helpers](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/include/sl_helpers_vk.h)
- [Core feature and resource types](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/include/sl_core_types.h)

The reviewed public headers name `sl::kFeatureDLSS_NR` and uplift color tags.
They are not a complete NR options/evaluation specification. Do not invent a
`sl_dlss_nr.h` ABI, reuse SR options without evidence, or call a DLAA result NR.
Resolve the matching runtime's exact feature contract before arming evaluation.

The outline loads the signed interposer, uses an explicit project GUID,
disables OTA loading, requests NR, obtains Vulkan requirements, exposes creation
proxies, and queries the selected physical device. It returns bypass for frames.
It does not implement motion, output/history allocations, tagging, or inference.

For the outline, set `XEMU_STREAMLINE_DIRECTORY` and
`XEMU_STREAMLINE_PROJECT_ID`; `XEMU_STREAMLINE_LOG_DIRECTORY` is optional. Use
this integration's own identity, not another game's application/project ID.

Xemu presents its Vulkan image through OpenGL rather than a Vulkan swapchain.
Streamline's normal per-frame presentation lifecycle therefore needs an explicit
solution. Obtain documented guidance for an evaluate-only path or implement a
validated bridge; do not call guessed private maintenance functions.

### NVIDIA Optical Flow SDK

The proposed first motion provider is Optical Flow SDK 5.0, subject to the
selected NR runtime's input contract and actual 3090 testing:

- [SDK](https://developer.nvidia.com/opticalflow-sdk)
- [Programming guide](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html)
- [Release requirements](https://docs.nvidia.com/video-technologies/optical-flow-sdk/read-me/index.html)
- [Integration and synchronization design](nvofa-vulkan-integration.md)

This is not implemented in the draft. The native Vulkan queue/semaphore handoff
cannot be substituted with a call inside an unfinished graphics command buffer.
Always estimate motion from consecutive original frames, never enhanced output.

### Vulkan and community research

- [Vulkan SDK](https://vulkan.lunarg.com/) for headers and validation.
- [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder)
- [Vulkan/D3D12 bridge](https://github.com/AlanBacker/dlss5-vk-bridge)
- [Reported 3090 OpenMW setup](https://github.com/magnesiumdreamz/openmw-zink-dlss5-kit)

Community projects are research references, not proof that this xemu adapter
works, and are not automatically redistributed. Proprietary models, DLLs, and
drivers are not included. Verify acquisition, licenses, and runtime identity.

## Runtime evidence

`collect-runtime-manifest.ps1` records file hashes/versions, signature status,
OS build, GPU identity and driver version without modifying drivers or binaries:

```powershell
.\contrib\neural-present\collect-runtime-manifest.ps1 `
  -XemuPath C:\src\xemu\build\qemu-system-i386.exe `
  -AdapterPath C:\adapters\xemu-neural-present-vulkan-copy.dll `
  -StreamlineDirectory C:\SDK\streamline-sdk-v2.14.1\bin\x64
```

Review the manifest before sharing: it contains local paths and computer name.
Attach evidence to this emulator PR/issue, not evidence-only PRs in the test-suite
repository. Keep emulator correctness captures before enhancement distinct from
post-enhancement visual-quality captures.
