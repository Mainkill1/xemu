# PGR2 current-main host profile: prioritize resampling qualification

## Decision and original outcome

**HOLD.** One separately named 30-second root-cause diagnostic preserves the
prior failed 300-second frame gate. It reaches the parked Hong Kong race and
records the complete perf trace, but native report processing times out.
The runner stops xemu and never reaches the after-context/end-capture steps.
Original outcome remains **failed / failed / incomplete / ineligible**.
Offline attribution cannot turn it into a qualified performance comparison.

The useful signal is **29.975% of process sampled cycles in libsamplerate**,
spread across the eight automatic VP workers. That merits returning to
[resampling semantics research #188](https://github.com/Mainkill1/xemu/issues/188)
before a broad Vulkan completion rewrite. It does not prove a converter
change is safe, predict an FPS gain, qualify audio output or support replacing
sinc by default. No emulator/library/runtime change exists in draft PR #302.

## Source and procedure

- Steam Deck **10.0.0.123**, main `76c23c7d444a6f12c9778bb2c35fab513f6c8056`;
  executable SHA-256 `5a3e3d8bc02abb602c1167ec19bff60afdbd072db791196f764cb34dad01c97b`;
  ELF build ID `030e46aa0d358865bf27ecdfc6c21e928d9994a8`.
- HTTP runner `e17919c849b840d43171a615b288f559fbec9e4c`; application
  `i250-76c23c7d-host-profile-001`; request `i250-host-profile-20261003-001-t001`;
  run `20261003-162005404-45b117287d1041c99ca8628fc5a74e41`.
- Saved recipe `i250-pgr2-host-profile-30-v1` revision
  `094e3eb21a65c53ac1fa72cf8c5a0cb3d5e20f2b4320eeb9590fa97dbb1a64a5`;
  [retained authored job, contract and workflow](../../performance/recipes/pgr2-host-profile-30/README.md).
- Exact prior navigation, 60-second settling, executable/configuration/firmware/
  PGR2 save and disc hashes, two-library package and full DSP/default JIT.
  Vulkan/RADV; existing Vulkan telemetry ON; no TCG/hybrid observer.
  `audio.vp.num_workers=0` means **automatic**, not no workers:
  `voice_work_init()` selects logical cores and clamps to `MAX_VOICE_WORKERS`;
  eight here. Do not describe setting zero as a serial-voice baseline.
- Three declared diagnostics: before procfs/sysfs context, 30 seconds perf at
  99 Hz with `dwarf,4096`, after context. All pause/resume flags false. Context
  source is maintained runner draft #105 (`ef8c763`), packaged/pinned by hash.
  No manual input, preview, active bulk transfer or host policy change.
- Private cold application/Mesa namespace and guest state qualified without
  waivers. This storage gate remains separate from overall failure. Full
  original state/input/effective-config ledgers are retained.

## Complete offline leaf attribution

| Complete leaf attribution | Samples | Estimated sampled cycles | Share of process sampled cycles |
|---|---:|---:|---:|
| xemu, exact ELF | 3,618 | 73,012,085,570 | 45.269% |
| libsamplerate.so.0.2.2 | 2,255 | 48,346,278,187 | 29.975% |
| Unresolved generated-code map | 1,231 | 28,818,188,737 | 17.868% |
| libm.so.6 | 144 | 3,070,556,613 | 1.904% |

**8,224 samples**, estimated event period **161,286,694,382 cycles**, no lost
samples reported. Shares use summed recorded event periods, not sample counts,
wall-clock time, blocked time or predicted savings. All leaf records were
parsed, including unknown symbols and small/kernel-looking addresses. These
are exclusive leaf groups; do not add inclusive callgraph totals to them.

The exact-ELF named leaves include `address_space_ldl_le` **6.978%**,
`voice_process` **4.600%**, `address_space_translate_internal` **3.848%**,
`flatview_do_translate.isra.0` **3.444%**, `voice_get_samples` **2.524%**,
`flatview_translate` **2.286%**, and `qemu_ram_ptr_length` **1.694%**.
Shared memory helpers serve multiple consumers; these numbers alone do not
prove all their work is eligible APU caching. Thread/DSO rows and every leaf
symbol are retained, not just favorable rows.

The eight TIDs 99090–99097 have voice-processing symbols and collectively
hold the libsamplerate work. TID99103 has named Rust DSP-JIT helpers plus
unresolved generated code; its largest unresolved group is not relabeled as
an exact DSP operation. The 17.868% generated-map group includes more than
one anonymous executable mapping. Only the **before** memory snapshot exists;
there is no after-generation or perf-map file proof. Unresolved individual
libsamplerate symbols remain unknown: the library DSO is identified, its
converter function names are not recovered from mismatched local libraries.

Production `voice_resample()` currently constructs **SRC_SINC_FASTEST** for
both ordinary mono and stereo voices. This source fact and the observed DSO
cost support #188's qualification priority, not hardware equivalence of a
cheaper converter. That issue already preserves a historical different-parent
linear experiment. Do not combine its reported gain with this unpaired
current-main profile.

## Native report failure and bounded offline recovery

Perf record writes **35,875,544 bytes**, SHA-256
`7b9c3d0de0d5e8d1a8881b2f756d15bf326d117b481996ef5ad4ade764028a7c`.
Recording/finalization complete; report generation or its output drain exceeds
60 seconds. The runner marks the diagnostic failed, terminates xemu (exit
137, **RunnerTerminated=true / Crashed=false**) and retains the trace. No
end scene or after-context snapshot exists. Directly viewed start capture
confirms 0 MPH, gear1, position6/6; the full profile scene is not qualified.
No automatic replacement attempt or longer tolerance was used.

Local `perf buildid-list` verifies the exact executable against its ELF.
`--symfs` maps only that recorded executable to the frozen package; unmatched
Deck system libraries and generated names remain unresolved. With local
`DEBUGINFOD_URLS` empty, complete `perf script -G` leaf output and flat
`perf report --no-children -g none` each exit 0 in about 0.065 seconds. The
ordinary report/callgraph mode on the same trace exits0 in 18.263 seconds.
These processing costs differ in analysis work, host speed, perf version,
libraries and symbol availability from the Deck; they do **not** establish
the native timeout's root cause. No runner source, host environment or timeout
policy was changed. The raw DWARF/callgraph data remains available for later
review; flat attribution avoids claiming an inclusive stack result.

## Next bounded experiment

Resume #188 with a production-linked state/audio fixture before a selector:
parameterize converter/channel/ratio, finite source exhaustion and 32/64-frame
boundaries; record fetched/generated/mixed frames, CBO, ACTIVE, stream segment
and notifications. Preserve different interpolation/read-ahead semantics rather
than inventing identical sinc/linear PCM. Cover loops, starvation/recovery,
voice reuse, reset/save/load and pitch changes before an optional mode decision.
Sinc remains default; hardware-accuracy/default replacement is separate.
Any retained candidate needs matching builds, fresh A/A, private qualified
cache state, ABBA/BAAB and valid correctness/work-time controls. No Improvement
% exists from this capture. Keep #250's ownership prototype on hold: its prior
300-second complete buckets measured only 524.040 ms of buffer-space fence
wait and do not establish the cause of the slow race cadence.

## Retention and limits

**880/880 eligible artifacts collected, zero runner exclusions**, all hashed
and retained locally. Public ZIP includes every measurement/log/capture,
original failed assessment/result, before snapshot, raw perf trace, exact
mapping commands and complete offline outputs. Binary `state/` cache contents
stay local with complete public per-file hashes and original ledgers. No
private executable, firmware, disc or guest HDD is published. The earlier
300-second frame failure and its exit 0/shutdown GLib assertion remain in
[their own packet](../issue250-finish-native-20261003/REPORT.md).

Perf sampling, context readout, Vulkan telemetry, screenshot overhead, cold
shader startup, host clocks/scheduling and unavailable end scene limit this
inference. No audio listening, waveform/state parity, representative driving,
matched baseline/candidate, throughput improvement or clean-shutdown claim.
All emulator evidence stays in **xemu**, while runner source stays in its
actual tool PRs. Current source/tests remain unchanged from the 24-check
reader and all 40 CI checks on documentation head `8c2c3a3972a`.

[Complete exclusive leaf/thread/DSO results and source/run bindings](SUMMARY.json)
· [Original raw trace, failures, snapshot and offline reports](EVIDENCE.zip)
· [Public payload hashes](INDEX.json).
