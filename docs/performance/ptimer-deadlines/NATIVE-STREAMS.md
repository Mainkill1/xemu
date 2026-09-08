# Native snapshot-stream qualification — partial, held

Built and executed source `e1b2e635def0d66a2911b94e005a988a3987e8b0`.
Emulator SHA-256: `c69d221d85aff9d9b8d9f9c94f63a11145ec93a4d1962c79f4cef3b0d2e671d4`.

The pinned Windows GCC build completed, including the Meson PTIMER unit target.
That unit passed **23/23 natively on Windows**. The build uses the same optimized
recipe as the previous baseline: optimization2, full LTO, x86-64-v3, debug symbols.
For precision, Meson records `buildtype=debug`, `debug=true`, `optimization=2`;
it is not an unoptimized O0 build. These settings match the previous measured
candidate's recorded options. No performance comparison is claimed here.

## Real streams exercised

A private Morrowind HDD clone loaded the existing snapshot. Native HMP `savevm`
created a temporary snapshot, and HMP `loadvm` loaded it and the old snapshot over
the same running process. All three commands returned successfully. Trace shows:

```text
vmstate_load_state nv2a v5
vmstate_load_state_field nv2a:ptimer.alarm_armed exists=1
vmstate_load_state nv2a v4
vmstate_load_state_field nv2a:ptimer.alarm_armed exists=0
```

[Full PTIMER field slice](native-stream-fields.txt) · [outcome CSV](native-results.csv)

This is execution of real NV2A VMState streams and the enclosing device hooks,
not just direct calls to a unit restore helper. It verifies version5 field
presence and version4 omission/loading for these streams. Versions1–3 and an
exhaustive matrix of masked/pending/stopped states are not covered natively.

## Qualification failed; preserve both unfavorable findings

**Paused-save ownership:** the first attempt explicitly stopped the VM before
saving. Initial gameplay resumed and emitted276 display writes in its10-second
observation. Save returned success, but the following continue command timed out
and normal close failed. The process required owned termination. No NV2A load was
reached. The source-level notification/unconditional-unlock mismatch is inherited
from frozen S and is tracked separately in [issue49](https://github.com/Mainkill1/xemu/issues/49).
The normal save path handles its own stop/resume and did not encounter that
command timeout in the subsequent attempt. A stack trace was not captured.

**Controller resume:** the subsequent attempt using normal save/load never left
the controller-reconnect screen, even before saving. It had zero display-write
events and identical captures across its observation windows. Successful stream
loads and QMP running state therefore do **not** qualify guest progression.

One unchanged10-second Vulkan Morrowind cell on the existing baseline executable
`fe8047f60181672170524cf40b2151f335f0e232d749b569b60fd5bc2cb5d6ba`
(source `3caf5d85423298c62344f8fe44fe9a1176b8ae81`, cumulative-S equivalent)
failed the same activity gate and also showed the reconnect screen. This proves
the symptom is not unique to the candidate, not that the candidate has no
regression or that its cause is known. No further game reruns were launched.

## Isolation and disposition

All jobs used the existing Session1 GUI launcher and unchanged Morrowind control
scripts. The new narrow job was corrected once to remove its unnecessary
explicit pre-save stop; the failed attempt and both job versions were retained.
The input sequence remained five seconds, Start, two seconds, B, two seconds.
No baseline snapshot/configuration or installed runner was rewritten.

The two failed candidate private disks were retained; the baseline cell removed
its owned private disk. Owned processes are stopped. The original immutable seed
hash was verified unchanged by the controller on closure.

**Keep PR48 draft and issue40 open.** Real version4/version5 deserialization and
the native unit build now have evidence; successful gameplay continuation does
not. Investigate the shared reconnect failure before relying on more Morrowind
measurements, and handle issue49's save lifecycle separately. No FPS, frame-tail,
CPU, power, XISO or native ARM qualification is added by this report.
