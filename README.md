Please visit [https://xemu.app](https://xemu.app) for more information.

## ENG-2026-523 XISO live-marker test harness

This branch adds an opt-in guest-event receiver used only by the performance
lab. It is inactive unless both `XEMU_PERF_GUEST_MARKERS=1` and a non-empty
`XEMU_PERF_LIVE_MARKER_PATH` are present. When enabled, I/O port `0xE9`
advertises marker protocol support, writes F0/F1 measurement boundaries, and
decodes the version-1 framed CONTEXT, HEARTBEAT, FAIL, and PASS events expected
by `xemu-perf-lab`.

The patch does not add renderer telemetry, queue waits, GPU-completion calls,
or production settings. Apply this exact commit to both sides of every matched
A/B test so the harness cannot be mistaken for the feature under test. Release
and Debug builds use the same pinned commands and compiler matrix documented by
the target performance branch; this commit changes no build flags.
