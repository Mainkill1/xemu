# Current-main PGR2 Vulkan attribution

Source main e15b180, source-equivalent release 197654, Deck RADV, existing Auto-four policy. Run 20261006-104710678-a0e0fb8ffc4c47aa9d07f2c0e7b2705e. Saved diagnostic a7427cec adds telemetry/diagnostic identity to original PGR2 route bbdb6657; inputs, waits, controller, timeout and analysis boundary preserved.

Start image is the Hong Kong flyover, end is parked 000 MPH. This does not qualify stationary A/B or a Windows comparison. The runner marks its automated checks passed/eligible; manual scene review overrides that interpretation.

## 25-second frame window (236 records)

- NEED_BUFFER_SPACE: 441 submissions, all timed, 3,821.612 ms total fence wait / 16.193 ms per guest frame.
- Descriptor-capacity requests: 236; uniform-capacity requests: zero. Capacity currently 1,024 sets. This only classifies part of the 441 buffer-space submissions; do not attribute all wait to descriptors.
- Descriptor writes: 415,881 / 1,762.2 per frame; texture-change publication requests 370,809; uniform-write requests 176,347.
- Draw-flush measured interval 46.692 ms/frame; pipeline preparation 19.325; descriptor update 9.636; texture binding 7.536. These nested host elapsed intervals may overlap and include waits: they are not additive exclusive CPU times.
- Surface-down and surface-create finish submissions zero; auxiliary surface uploads zero. CPU readback remains 4.396 ms/frame.
- Whole stationary segment reports process CPU 374.98% (one core=100%); five-interval guest cadence 9.138/s. These are diagnostic observations across the existing scene transition, not stable parked throughput.

## Next bounded question

Classify the remaining 205 capacity drains and inspect descriptor reuse/capacity before changing ownership. A larger descriptor capacity may defer work to a later drain; it does not prove total work or critical-path wait decreases. Do not remove guest-readback/report waits. #250 remains architecture HOLD. No renderer changes from this recording.

Only numeric counters and provenance are published; game images/resources remain private.
