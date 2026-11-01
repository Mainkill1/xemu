# GPU depth-view aliasing for the Conker menu

## Goal and measured context

Bring Conker: Live & Reloaded's saved bar/menu scene to a stable 30 guest
frames per second on the Steam Deck while preserving the game's depth tests
and guest-memory visibility. This design addresses one measured bottleneck,
not the whole frame budget. The combined draft stack #282–#284 runs at a
directional 11.36 FPS versus 8.15 FPS for its parent. A temporary native
profile of that stack measured a median six synchronous surface readbacks
costing 31.63 ms per guest frame. Most of that time was large depth data.

The scene repeatedly binds a 640×480 linear Z24S8 target and a 32×32
swizzled Z24S8 view at the same guest address. The small pass has an enabled
`LEQUAL` depth test, so discarding its depth input is incorrect. A local
trial that merely folded depth downloads into the active command buffer
reduced Vulkan queue submissions without improving frame rate.

## Current ownership boundary

`SurfaceBinding` owns one host image. `surface_put()` invalidates overlapping
bindings, and a dirty image is synchronously downloaded to guest RAM before
it is invalidated. The next view then uploads from RAM. This is correct for
arbitrary guest access and writes, but forces GPU→CPU→GPU transfer for the
Conker alias pair. The existing depth-to-texture path already packs a Vulkan
depth/stencil image to a buffer without reading it on the CPU; the existing
surface upload path unpacks packed guest bytes into a depth image.

## Intended behavior

For an eligible read-only alias, create the small depth image directly from
the large image on the GPU. Pack the large image's Z24S8 values to a linear
buffer, interpret the first small-view byte range in guest Morton order,
then unpack the resulting 32×32 linear depth/stencil values into the small
image. The producer image remains owned and dirty. On return to it, rebind
that same image when the alias made no writes and no guest write superseded
it. No guest-memory materialization is needed for this GPU-only handoff.

The first implementation accepts only same-address, same-Z24S8-format,
unscaled, non-antialiased aliases where the small view is swizzled and
provably read-only: depth and stencil writes disabled, no clear, and no
other render-target role at that address. It must compare the effective
surface address, DMA scope, dimensions, and required byte extent; a shader
hash or guest address alone is not an identity. Any unsupported case uses
the current download/invalidate/upload path.

Guest CPU reads or writes of the producer's dirty range must still trigger
ordered materialization or invalidate the GPU alias. A texture or blit that
consumes the overlapping bytes through another route must receive the
correct version or force the old path. Save-state capture, renderer reset,
surface eviction, and title change materialize or discard the owned images
under the existing synchronization rules. An inactive producer cannot be
freed while a recorded command still references it.

Only the active view supplies the draw's depth attachment. The inactive
producer remains retained for coherence; it must not be mistaken for the
current guest-memory bytes. The UI and trace should identify GPU alias
conversions separately from actual GPU→RAM downloads.

## Reviewable milestones

1. **GPU conversion primitive.** Add a tested Morton mapping and a Vulkan
   compute conversion from packed linear source depth to packed linear
   destination depth for the swizzled view. This change does not select
   aliases or alter existing surface lifetime behavior.
2. **Read-only alias ownership.** Keep an eligible producer image alive while
   the small read-only view is bound, convert on the GPU, and restore the
   producer on return. Add explicit fallback and invalidation gates for guest
   writes, texture use, clears, format changes, and resource retirement.
3. **Native qualification.** Verify unedited output against retained
   screenshots and focused alias/guest-access fixtures, run affected and
   control XISO cases with per-test timings, and compare Deck/Windows title
   behavior. Keep each code mechanism in its own PR; do not merge merely
   because the scene looks plausible.

## Acceptance evidence

- A focused test alternates a linear depth producer and swizzled read-only
  consumer. Its final color/depth output and guest read bytes match the
  existing path for D24S8 and D32S8 host formats where supported.
- Morton mapping covers non-square power-of-two dimensions as well as the
  32×32 Conker case. A partial guest write, CPU read, texture alias, clear,
  depth write, stencil write, unsupported format, scale change, and pending
  command each take a proven coherent path.
- The saved Conker menu shows the same game-camera image and a lower count
  of large depth GPU→RAM transfers. Use the same snapshot, segment, renderer,
  settings, and binary/source identities for A/B/B/A then B/A/A/B runs.
  Report FPS plus average, p95, p99, and maximum frame time, including any
  slower outlier. Driver-cache state must be qualified rather than accepted
  away. A sustained 30 FPS is the overall objective; a gain below that keeps
  the goal and PR recommendation open.
- Both Vulkan and OpenGL behavior remain correct. The new fast path is
  Vulkan-specific; OpenGL is an unchanged control, not proof of Vulkan
  correctness.

## Limits and rejected shortcuts

This design does not skip the small depth test, infer object identity, alias
incompatible Vulkan image allocations, or treat the current guest RAM as
fresh while a dirty producer remains on the GPU. Merely reducing submission
count was tested and did not improve the title. Full-frame performance also
includes roughly 780 draws per guest frame and significant pipeline and
emulation work; removing depth readbacks alone is not proof of 30 FPS.
