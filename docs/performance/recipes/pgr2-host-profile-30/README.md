# PGR2 host profile: retained 30-second diagnostic recipe

This recipe follows the exact frozen executable, private cold Mesa/guest state,
input binding, menu navigation and 60-second settling of the adjacent
[PGR2 finish recipe](../pgr2-finish-300/README.md). It is a separate diagnostic,
not a replacement for that recipe's failed frame-evidence qualification.

It records a start scene, takes a read-only procfs/sysfs snapshot, captures
30 seconds at 99 Hz with the maintained runner perf adapter (`dwarf,4096`),
then requests another snapshot/end scene. All pause/resume flags are false.
Package the adjacent frozen configuration/inputs and the maintained
`tools/linux_performance_context.py` from Xemu-Test-Runner draft #105
(`ef8c763`). Its SHA-256 is pinned in `job.json`; this repository does not
copy that tool's implementation or publish guest assets.

The actual immutable saved definition is `i250-pgr2-host-profile-30-v1` at
`094e3eb21a65c53ac1fa72cf8c5a0cb3d5e20f2b4320eeb9590fa97dbb1a64a5`.
Follow the adjacent upload/definition-readback/select/explicit-start workflow
with this recipe's test ID and unique application/request IDs. Verify all
32 authored steps and all three diagnostic controls. Follow the same attempt
to terminal state and collect all artifacts after idle; a timeout is not a
replacement-run instruction.

## Preserved first result

Request `i250-host-profile-20261003-001-t001` recorded 8,224 samples/no lost
samples, then the runner's perf report exceeded its 60-second processing
limit. The runner terminated xemu (exit 137, runner-terminated, not crash).
The after snapshot and end scene are absent. Original assessment remains
failed/failed/incomplete/ineligible. Do not lower a frame floor, approve the
scene from a nonblack image, or mark this performance qualified.

The raw trace can still be read offline with the exact ELF mapped into
`perf --symfs`. Verify the executable's build ID against `perf buildid-list`.
Use `perf script -G` for complete leaf samples and
`perf report --stdio --no-children -g none --percent-limit 0` for flat
attribution; preserve unknown DSOs/symbols and all samples. The owning PR
retains commands, outputs, source/executable identity and the original failure.
Native and local library versions, symbol availability, host speed and report
mode differ: offline recovery does not explain the native timeout or repair
the native outcome.

Setting `audio.vp.num_workers = 0` means **automatic**, not no workers;
production `voice_work_init()` selects/clamps logical cores (eight here).
Sinc remains the production converter. A different converter changes audio
and fetch/read-ahead semantics and needs separate qualification under #188.
This profile cannot estimate a converter speedup or qualify audible output.
