# Issue #233: native audio ownership and cost preflight

**Shared-output optimization: HOLD.** The shipping Deck release uses QEMU's
silent `none` backend for AC97. Its separate APU monitor uses SDL3 in production
source; there are not two audible host-output paths to consolidate in this
baseline. Adding audible AC97 output would change functionality and work, so it
cannot be scored as an equivalent-work speedup. No shared-output candidate or
emulator runtime change is implemented here.

This branch retains a useful diagnostic:
[`scripts/xemu-audio-state.py`](../../../scripts/xemu-audio-state.py), sourced by
GDB with matching DWARF, reports the selected implicit backend and hardware/
software voice state. It performs no inferior calls, memory writes or breakpoint
changes. It refuses an existing report and fails if no live inferior attached.
The separate runner launcher supplies process-specific debugger consent at exec;
its code/tests belong to Xemu-Test-Runner. Native results stay in xemu.

## Exact native inputs

Steam Deck **10.0.0.123**, PGR2 cold start, Vulkan, full DSP/default JIT,
VP workers 0, 128 MiB, vsync off, no explicit `-audiodev`, exact parent runtime
libraries, private HDD/EEPROM and cold private Mesa namespace. Fork main was
rechecked at `ee5ce48b48784f999af374c1452003f8b2b1230f`.

- Release executable SHA-256:
  `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`.
- ELF build ID: `7cc47cc0deb81956e198d71935d1a198116cdb02`.
- Matching DWARF SHA-256:
  `614ec7555af52d876e51de31263126b7fe700aac87c9592c218a959a24fffb0b`.
- The diagnostic waits 30 seconds, queries QMP, pauses through the maintained
  runner, inspects typed state with GDB, takes a screenshot, then quits. It has
  **no throughput timing or baseline/candidate comparison**.

## Results and failures

| Attempt | Method | Execution / evidence | Observation |
|---|---|---|---|
| v1 | Direct release launch | Failed / incomplete | QMP found implicit backend; GDB denied by `ptrace: Operation not permitted`; no state report |
| v2 | Process-specific runner debugger consent, then exec same release | Completed / complete | GDB attached; exact native executable hash matched; backend `none`, active `ac97.po` voice |

v1 run: `20261002-023804001-0fe7111eb706457897ff17a0e9f891c9`.
v2 run: `20261002-024546727-7f7556cb3d5c465d9187e76446fc2b80`.
The original failed attempt stays failed; v2 is an intentional changed diagnostic
procedure, not an outcome repair or replacement performance sample.

| Live field | Observed value | Interpretation |
|---|---|---|
| `query-audiodevs` | `[]` | Explicit backend list only; not proof of no backend |
| `qom-list /audiodevs` | `#default: child<audio-backend>` | Implicit backend exists |
| Driver name / enum | `none` / `AUDIODEV_DRIVER_NONE` | No physical output from QEMU AC97 backend |
| Hardware output | Enabled, 44,100 Hz, 2 channels | Rate/format of generic QEMU backend |
| Software playback voice | `ac97.po`, active, 48,000 Hz, 2 channels, mute false | Codec playback path is configured and active at snapshot |
| Timer period | 10,000,000 ticks (virtual ns) | Existing 10 ms backend period; not changed |

[Raw state](audio-state.json), [v1 canonical result](failed-terminal.json),
[v2 canonical result](completed-terminal.json). The runner declared v2 eligible
under its diagnostic artifact contract; that does **not** make it a performance
comparison. The [original screenshot](audio-state.png) shows PGR2's Bizarre
Creations startup logo, not an active race. `vmRunning: false` reflects the
requested diagnostic pause. Empty/mixed-sample fields are instantaneous state,
not cumulative PCM production/consumption or audibility proof.

The v2 launcher logs target PID **48815** and owning runner PID **33638**; GDB
inspected that same PID. The launch hash belongs to the Python launcher, while
the verified `role: emulator` input and GDB's `/proc/PID/exe` hash identify the
unchanged release. Neither run changes a global ptrace setting or grants an
unrestricted ptracer. The specific owner relationship is described by the
[Linux kernel Yama contract](https://docs.kernel.org/admin-guide/LSM/Yama.html).

Both state reports pass private Mesa disk-write qualification without a cache
waiver. v2 exits normally, but stderr contains a nonfatal GLib `g_source_destroy`
reference-count assertion at shutdown. That line remains visible; this report
does not claim general emulator correctness, clean shutdown diagnostics or
audio parity from successful GDB inspection.

## Existing steady-state profile: cost is not a promising Deck target yet

The existing exact-parent #234 C/JIT profiles are reused as **diagnostic
attribution only**. Both are canonically performance-ineligible because their
90-second frame evidence missed the 160-positive-interval floor. No new profile
or speedup is claimed here.

| Existing profile | Matching generic audio/AC97 leaf samples | Matching sampled process cycle period |
|---|---:|---:|
| Full C DSP | 0 | 0.00000% |
| Default JIT | 16 | 0.04237% |

The JIT rows include six `st_rate_flow_mix`, three `AUD_write`, three
`audio_timer`, two `po_callback`, and one each `mixeng_volume` / `audio_reset_timer`
samples. The attribution sums only identified native-xemu leaf symbols, not
whole call trees, generated code, unresolved addresses or library time. Sampling
skid, inlining and shared generic helpers limit this estimate; low/zero samples
do not prove zero cost. This stationary PGR2 observation is **not** qualification
of an AC97-heavy FMV title or all games.

[Exact attribution/digests](existing-profile-audio-attribution.json) and exported
leaf records are retained in the packet. The original raw profiles and canonical
failures remain in [#281's owning evidence](https://github.com/Mainkill1/xemu/blob/perf/issue234-dsp-dispatch/docs/evidence/issue234-dsp-dispatch-20261001/native-profiles/README.md).

## Evidence and retained tools

[Native packet](native-records.tar.gz), [included/omitted SHA-256 manifest](manifest.json.gz),
[archive verification](verification.json) retain **109 payload files**, including
both attempt results, launched jobs, runtime/input identities, config/cache
reports, debugger/QMP/stdout/stderr, source probes, frozen launch manifests,
screenshots and diagnostic ZIPs. All archive payload hashes were reread after
packing. Raw cache files and matching release DWARF remain local/server-side
with omitted-byte hashes. No executable, firmware, game image or private HDD
is redistributed.

`native-records.tar.gz`: 1,026,094 bytes; SHA-256
`d8b08f4d04b758d32b17a0744ad734bb12841536922342a0530e082026caeb23`.
The runner launcher's six ancestry/refusal tests pass after three expected red
failures; those logs are retained here, not as emulator evidence in the tool repo.

To repeat the inspection intentionally, package the unchanged xemu and its
matching `.gnu_debuglink` file, use the runner diagnostic launcher and an external
GDB recipe with `--se {packageDir}/xemu --pid {pid}`, then
`source {packageDir}/xemu-audio-state.py`. Run GDB in the result directory so its
`audio-state.json` has a stable artifact path. Every new procedure gets a new job
ID and runs/collects through the maintained HTTP client. Do not attach this probe
to clean benchmark windows.

## Decision

Do not prioritize a shared-sink performance candidate for the current Deck
baseline. First establish an audible AC97 reference and material cost in an
AC97-heavy workload before reopening that comparison. A future shared sink may
provide useful functionality, but preserve volume, rate conversion, partial
acceptance, BUP, independent playback and lifecycle semantics rather than scoring
lost or newly added PCM work as a speedup. No claim is made about Windows or
other host backends; #233 remains open for those unmet prerequisites.

The next Deck performance investigation is #246's texture-image allocation
churn. Attribute create/destroy cost and compatible configuration reuse before
adding a bounded pool. The useful debugger/inspection tools remain maintained
for subsequent investigations.

> Agent declaration: research, tools and evidence prepared with Codex (GPT-6).
