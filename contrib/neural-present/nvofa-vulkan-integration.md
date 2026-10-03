# NVIDIA Optical Flow SDK 5.0 Vulkan motion provider

## Purpose

This is the concrete next-stage design for producing estimated motion vectors
from two consecutive **original** xemu display images on an RTX 3090. It is not
wired into the reference adapter because the NVOFA Vulkan API executes on a
dedicated queue through `NvOFExecuteVk`, while the current adapter ABI records
in xemu's graphics command buffer. The synchronization boundary must be added
explicitly.

Official references:

- <https://developer.nvidia.com/opticalflow-sdk>
- <https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html>
- <https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-application-note/index.html>
- <https://docs.nvidia.com/video-technologies/optical-flow-sdk/read-me/index.html>

NVIDIA documents Vulkan support in Optical Flow SDK 5.0. Ampere supports 1x1,
2x2, and 4x4 grids. Use 1x1 only after measuring its cost and confirming that
the downstream NR contract expects dense pixel motion; 2x2 is a practical first
quality/performance point.

## Device creation requirements

Before `vkCreateInstance`/`vkCreateDevice`, the adapter must request:

- Vulkan API 1.3;
- `VK_KHR_get_physical_device_properties2` at instance level where required;
- `VK_KHR_timeline_semaphore`;
- `VK_KHR_synchronization2` or the promoted Vulkan 1.3 feature;
- `VK_NV_optical_flow`;
- `VkPhysicalDeviceTimelineSemaphoreFeatures::timelineSemaphore`;
- `VkPhysicalDeviceSynchronization2Features::synchronization2`;
- `VkPhysicalDeviceOpticalFlowFeaturesNV::opticalFlow`; and
- one queue from a family advertising `VK_QUEUE_OPTICAL_FLOW_BIT_NV`.

The optical-flow queue is not interchangeable with xemu's current combined
graphics/compute queue. The adapter's pre-device create proxy must enumerate
queue families, preserve xemu's queue request, add the optical-flow queue, and
remember both family/index pairs.

## Required external SDK files

From the official Optical Flow SDK package, use the Vulkan headers and sample
matching SDK 5.0. Load the driver-installed runtime dynamically:

- Windows: `nvofapi64.dll`;
- Linux: `libnvidia-opticalflow.so`.

Do not package driver runtime binaries with xemu. Check
`NvOFGetMaxSupportedApiVersion` and resolve the Vulkan API entry table exposed
by the SDK package.

## Resource set

Use a bounded ring of at least three frame slots. Each slot owns:

- normalized current-color `VkImage`;
- normalized previous-color `VkImage` or a reference to a retired prior slot;
- NVOFA output flow `VkImage`;
- optional cost/confidence image;
- timeline semaphore values for graphics-to-OFA and OFA-to-graphics; and
- registration handles returned by `nvOFRegisterResourceVk`.

Never register xemu's GL-shared display allocation directly until its tiling,
format, usage, and ownership are verified against NVOFA requirements. The safe
initial design copies the original display image into adapter-owned optimal-
tiled input images.

Query supported formats using `nvOFGetSurfaceFormatCountVk` and
`nvOFGetSurfaceFormatVk` for each usage/mode. Do not assume
`VK_FORMAT_R8G8B8A8_UNORM` is accepted as both input and output.

## Per-frame sequence

```text
xemu graphics command buffer
  render final original display image
  transition display -> transfer source
  copy/convert display -> current OFA input
  signal graphics_ready timeline value
  submit graphics work

optical-flow queue / NvOFExecuteVk
  wait graphics_ready
  consume previous + current registered inputs
  write forward flow (+ optional cost)
  signal ofa_ready timeline value

xemu graphics continuation
  wait ofa_ready
  convert NVOFA fixed-point vectors into the NR motion convention
  execute DLSS NR adapter using original color + converted motion
  copy completed neural output into xemu display image
  return display image to xemu-required layout
```

`NvOFExecuteVk` accepts explicit input semaphore waits and returns an output
semaphore signal through `NV_OF_SYNC_VK`. Use timeline values monotonically and
do not recycle a slot until both queues have retired it.

The current single-command-buffer adapter callback cannot insert a CPU call to
`NvOFExecuteVk` between two portions of one already-recording command buffer.
Choose one of these implementations:

1. **Recommended first implementation:** make the neural adapter own the entire
   post-display submission. xemu ends/submits its display render at the adapter
   boundary, the adapter executes NVOFA, then records/submits NR and final
   presentation transitions. Extend the ABI with explicit prepare/submit/retire
   callbacks.
2. Split xemu's display command recording into pre-neural and post-neural
   submissions with timeline semaphores and invoke an adapter queue callback
   between them.
3. Use a compute optical-flow implementation in the existing graphics command
   buffer for the first prototype, accepting higher shader cost, then replace
   it with NVOFA after the NR runtime is proven.

Do not call `vkQueueWaitIdle` per frame. xemu's current display path already
waits for its one-time command submission; adding another unconditional device
or queue idle would serialize all work and hide resource-lifetime errors.

## Vector conversion

NVOFA outputs a signed fixed-point vector: horizontal and vertical components
are 16-bit values with five fractional bits. Convert explicitly to the motion
units required by the DLSS NR contract. Record:

- whether vectors are previous-to-current or current-to-previous;
- pixel, normalized, or render-resolution units;
- input and output grid size;
- active-rectangle crop and vertical orientation;
- processing/display extent ratio; and
- confidence/cost rejection policy.

Do not infer the sign by visual trial alone. Use an authored test where a solid
object translates by a known integer number of pixels and assert the vector
value/direction.

## Temporal rules

- Always derive motion from two original frames, never from neural output.
- Do not advance the previous-frame slot for a repeated xemu source key.
- On reset, set `disableTemporalHints`/the matching NVOFA invalidation field and
  clear the previous-image association.
- Treat resize, save-state load, renderer recreation, title reset, and frame-key
  discontinuity as resets.
- Preserve a short repeated frame during pause without declaring a camera cut.

## Tests

1. Static translated checkerboard: exact sign/scale.
2. Camera pan with stationary HUD: quantify HUD flow contamination.
3. Occlusion/reveal: inspect cost/confidence and reject invalid vectors.
4. Resize/reset: no use-after-free and first-frame bypass/reset.
5. 300-frame ring-buffer soak under Vulkan validation.
6. Compare 4x4, 2x2, and 1x1 modes on RTX 3090 for GPU time and NR artifacts.
