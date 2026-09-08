# Combined PTIMER and paused-save qualification

**Original attempt: FAIL. Focused reconnect follow-up: PASS; PR remains draft.** Tested source `45c773a22fdcdbf9a28e8be78dfb3f5aae9c8c7d` merges the independently qualified paused-save repair from PR #50 into the timer candidate. The recorded executable and unit hashes are in the [manifest](combined-manifest.json).

The combined source passed independent interaction review and 150 deterministic snapshot lifecycle attempts. Timer/PRAMDAC implementation and timer-unit sources are unchanged from the preceding reviewed timer head. The full optimized Windows build completed, and its native timer executable passed **23/23** cases. Previous failures and their source identities remain in [NATIVE-STREAMS.md](NATIVE-STREAMS.md).

| Native stage | Display writes in approximately 10 seconds | Activity result |
| --- | ---: | --- |
| Initial v4 snapshot, Start/B resume | 278 | Pass |
| New v5 snapshot save/reload | 279 | Pass |
| Reload original v4 over current state, Start/B | 0 | **Fail** |

[Per-stage CSV](combined-results.csv) · [compact stream trace](combined-stream-fields.txt).

All save/load/continue commands returned. The v5 stream deserialized `ptimer.alarm_armed`; the later v4 stream omitted it as expected. Normal process close completed, the failed private disk was retained, and no owned emulator remained. The unchanged controller's close check verified the original seed identity.

The final screenshot shows the game's controller-reconnect prompt, rather than resumed gameplay. Trace evidence distinguishes the snapshots: the new v5 state includes `usb-xbox-gamepad`, while the original v4 state restores no gamepad device. The original snapshot was deliberately captured with the controller disconnected. The job reused Start/B after live reload but did not explicitly reconnect the host keyboard controller. Source inspection finds no input-rebind call in the HMP snapshot-load path. This establishes a missing reconnect step in the test recipe; it does not prove that it is the sole cause of failed input delivery. The key helper also does not verify successful foreground activation.

## Completed reconnect follow-up

The same source and executable were subsequently checked with explicit keyboard-controller reconnection before Start/B after the live v4 restore. The worker recorded replacement of the controller instance, followed by **276 / 276 / 283 display writes** in the initial v4, new v5 round-trip, and old v4 reload observation windows respectively (approximately 10 seconds each). The final captured image was independently inspected and shows the expected gameplay scene without a reconnect or pause overlay.

[Follow-up manifest](reconnect-manifest.json) · [per-stage observations](reconnect-results.csv).

This supports an input/reconnection precondition explanation for the earlier failed live-reload attempt on this executable. Foreground handling and reconnection both changed, so this is not an isolated attribution of which input step failed. It is not evidence of a timer performance improvement. Cleanup completed, the private disk was removed, the immutable seed was unchanged, and no emulator process remained.

The follow-up used experimental menu-coordinate automation. That method is **not adopted as the standard test route**; normal qualification retains the established scripted snapshot-and-keyboard sequence. The installed proven runner was unchanged. Earlier failed attempts remain retained. No new performance campaign was run.

Remaining qualification includes the issue #40 fixed-work wakeup comparisons and the broader release gates in #38. These observations do not qualify every migration state or pre-v4 stream version. PR #48 remains draft.

No performance comparison was run and no FPS, CPU, power, or p95/p99 improvement is claimed. Existing integrated performance evidence remains attached to its original builds. Installed launch/input scripts were unchanged; this was a new single-purpose job with its own recorded hash.
