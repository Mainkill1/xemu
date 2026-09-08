# Paused snapshot save ownership

Saving an already paused VM skipped NV2A's SAVE_VM quiescence notification but still ran its unconditional post-save unlock. Issue [#49](https://github.com/Mainkill1/xemu/issues/49) records the native stop → save → continue failure: saving returned success, then continue timed out after 90 seconds. Source ownership is independently confirmed; no captured stack identifies the exact subsequent wait.

The repair gives ordinary snapshots and Xen device-state saves an explicit ownership scope. Running saves reuse notifier quiescence; paused saves quiesce before any serialization. FIFO/PGRAPH ownership remains held through output completion and is released exactly once on success or failure, restoring the prior halt state. Other notifier-owned saves retain their post-save release path. Host-only ownership fields do not change VMState format. Xen write failures also close the output file, avoiding the old short-circuit omission.

## Qualification

Tested source: `37e0968cbf0492451edd71ce2728d32c3098086f`, based on `bac2cae991a2d359eddfc6836dec3db60fba5618`. The report addition and test whitespace cleanup do not change the tested production code. See [manifest](manifest.json) and [native results](native-results.csv).

- **150 deterministic lifecycle attempts pass**, compiling the actual NV2A/save functions with lock/I/O doubles. Coverage includes repeated paused/running saves, prior halt state, both explicit entry points, notification-owned release, and applicable admission/open/write-before-section/write-after-section/close/create failures. Output-close and snapshot-create doubles verify ownership remains held.
- Parent negative control fails before RAM serialization in the paused-success case. The new Xen write-failure case failed its close-count assertion before the local file-close repair.
- Full optimized Windows cross-build completed using the pinned toolchain and the existing optimized recipe. Recorded Meson settings are `buildtype=debug`, `debug=true`, `optimization=2`, full LTO, and x86-64-v3; this is the existing optimized recipe, not an O0 build. No candidate-only optimization setting was introduced.
- **Native Windows/Vulkan Morrowind check passed:** two paused save/continue cycles, a running save, and reload of the saved NV2A v4 stream. Paused saves remained paused until explicit continue. The process closed normally, private HDD was deleted, and immutable seed hash was unchanged.
- Initial and final screenshots were manually inspected after execution: both show the expected game scene without the reconnect/pause overlay. The automated activity check and screenshots complement command success; command success alone is not gameplay proof.

| Native observation (approximately 10 seconds each) | Display-write events |
| --- | ---: |
| Initial resumed scene | 287 |
| First paused save/continue | 279 |
| Second paused save/continue | 286 |
| Running save | 290 |
| Saved-state reload | 284 |

These are activity counts, not rendered FPS or a paired performance comparison. No CPU, power, FPS, p95/p99 improvement is claimed. Existing integration performance data remains attached to its original source identity; it does not automatically qualify this revision.

## Reproduction

Run `python3 tests/unit/test-xbox-snapshot-lifecycle.py` for deterministic controls. The test accepts `--source-root` to use the same harness against the parent source.

For the focused native route, isolate a writable copy of the Morrowind snapshot HDD and EEPROM, verify the executable hash, and load snapshot `vm-20260905015459` with Vulkan through the interactive desktop session. Once running, wait 5 seconds, press Start (150 ms), wait 2 seconds, press B (150 ms), and wait 2 seconds. Observe 10 seconds. Twice execute QMP stop, verify paused, save a private snapshot, verify still paused, continue, and observe 10 seconds. Then save while running, observe 10 seconds, reload the private snapshot, continue, and observe 10 seconds. Collect timestamped display-write events and screenshots; close the owned process, verify exclusive HDD access and original seed identity, and remove only the private copy on success. Command timeouts or missing activity fail the run and retain failed evidence.

The installed launch/input controller was unchanged; its hash and the new single-purpose job hash are in the manifest. A failed source-bundle import was retained before the successful build attempt; it failed before compilation and produced no executable used for testing.

## Scope and remaining limits

The source review found no new snapshot/Xen lifecycle blocker. Xen runtime migration and real error injection are not qualified by the Morrowind run; their covered ownership/error cases use deterministic doubles.

Guest-memory dumping has an inherited different lifecycle: its SAVE_VM notification can retain NV2A locks without a serialization post-save callback, and detached completion can occur on another thread. This patch preserves that path and does not claim to repair dumping or COLO migration. They require separate lifecycle investigation. OpenGL and broader retail/XISO qualification are not newly claimed here. The focused test addresses the reported Vulkan paused-save failure without repeating the completed performance campaign.
