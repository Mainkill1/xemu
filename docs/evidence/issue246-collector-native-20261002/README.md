# #246: Deck allocation collector and negative controls

**Decision: HOLD.** The collector now closes a valid native shutdown checkpoint.
The accepted PGR2 window has no destroyed images and consequently no allocation
reuse opportunities. It does not justify a retained-image pool for this window.
Morrowind collector-ON has an unresolved PFIFO abort. No pool or performance gain
is implemented or demonstrated.

## Builds, host and method

All procedures ran through the maintained HTTP runner on **Steam Deck
10.0.0.123**, Vulkan, full DSP/JIT. Runner revision:
`848dca74e1ff79f9fc886769a785c23a0945a87e`.

- **A parent:** `ee5ce48b48784f999af374c1452003f8b2b1230f`, executable SHA-256
  `4a6e0dae0a83a870aebde726ca40687f35c9293f3da1147a6d7d14e8c4e8e01a`.
- **B collector:** `8079502ae25a602e1395fa9ee87ec1ef8b57c9d6`, executable SHA-256
  `1b52adf0dea0823a95c37ae79d447af01bf8a904d6385037908b2ca57d44bba2`,
  ELF build ID `d584912b97c1e515f9348cb57e99747196005e65`.
- CI toolchain metadata matches: Clang/LLD 21.1.8, Rust 1.96.0, GCC 12.3.0
  component. All procedures use the **pinned parent library bundle**; candidate
  packaged GLib differs. Identity and CI logs are archived.
- Private cold Mesa/application cache and private HDD/EEPROM, pinned firmware
  and discs, no VM restore. All five private Mesa disk-cache qualifications
  passed without waivers. OS/driver memory caches remain uncontrolled.
- The profiled procedure failed before its scene checks. The separate
  unprofiled recipe replaces each 30-second profiling slot with a 30-second
  wait. Morrowind A, B OFF and B ON use that same navigation/wait procedure.
  These are explicit distinct saved tests, not hidden retries.

## Original outcomes

Every attempt was fully collected, including failure reports. The archive
contains full assessments rather than relying on truncated summary responses.
Runner comparison eligibility is a diagnostic contract, not A/B acceptance.

| Saved procedure / archive directory | Execution and evidence | Allocation qualification |
|---|---|---|
| Morrowind B ON, profiled / `morrowind-profiled-failure` | Failed: startup `perf report` exceeded its 60-second limit; runner killed xemu, exit 137 | Truncated record; shutdown not reached |
| Morrowind B ON, unprofiled / `morrowind-on-abort` | Crashed: PFIFO DMA-range assertion, SIGABRT/exit 134 | Truncated record; shutdown not reached |
| Morrowind A, unprofiled / `morrowind-parent-control` | Completed; original correctness/evidence passed | No allocation collector |
| Morrowind B OFF, unprofiled / `morrowind-off-control` | Completed; original correctness/evidence passed | Allocation collection disabled |
| PGR2 B ON, unprofiled / `pgr2-on-checkpoint` | Completed, exit 0; original correctness/evidence passed | Full schema-2 validation passed |

The abort is `pfifo_run_pusher()` at `pfifo.c:297`,
`Assertion !"Dma value is out of range in PFIFO pusher"`. Core metadata is retained;
core export/analysis was unavailable. One passing A/OFF control cannot establish
that collection caused the abort. The profiler timeout is a runner tool failure,
not an emulator crash. Neither failure was repaired or omitted.

Run identities, in the same order:

```text
20261002-060754813-fa14ba135a464ae7b14e0ff204ebfdf0
20261002-061232591-604835e9e8f84162927b14a4e09fe595
20261002-061742118-accd14a63ead451081d9e604fecac64a
20261002-062420291-70415e0a4aba429ba9a0001eaebb2493
20261002-063023515-b3b4572c8e90494aa88319ceed4e7c9c
```

## Accepted PGR2 allocation result

The window covers boot/loading and the Hong Kong starting grid. Two screenshots
show **0 mph**; other cars move and the GO indicator clears. It does not cover
driving, repeated race/menu transitions or sustained texture streaming.
Morrowind controls show the initial ship interior/NPC behind a name-entry modal,
not open-world streaming.

| Allocation class | Successful creates | Total host elapsed time | Maximum single create | Destroy calls | Post-destruction reuse opportunities |
|---|---:|---:|---:|---:|---:|
| Eligible ordinary images | 797 | 12.100 ms | 0.123 ms | 0 | 0 |
| Surface-copy images, excluded from pooling analysis | 19 | 34.695 ms | 34.309 ms | 0 | Excluded |
| Total observed scope | 816 | 46.795 ms | 34.309 ms | 0 | 0 |

These are host elapsed durations around actual VMA calls, **not CPU time, frame
time, probe overhead or an improvement percentage**. Metadata/output overhead is
outside the timing brackets. Surface-copy and ordinary classes are disjoint;
binding/upload totals are nested and must not be added as independent work.

All 816 images and **63,171,584 bytes** remain live at the idle shutdown
checkpoint; this is traced allocation size, not total VRAM use/budget. The
reader identifies 78 eligible configuration classes and 19 excluded creations.
Repeated equal configurations still live simultaneously are not reuse hits.
The largest creation is a surface copy (sequence 29, renderer flip-stall count
410, submission 714, format 44, extent 1280×480, 2,621,440 allocation bytes).

The 540,136-byte original allocation JSONL hashes to
`c57dd054efb48b0e54d721ecf5c22b388e746024b3e261241d77be8b95bacf76`.
Its footer has `end_reason=shutdown_checkpoint`, no dropped events/clock errors,
and complete window accounting. It does **not** assert later resource cleanup
was observed. The full schema-8 telemetry reader also passed.

## Reproduce and inspect

Download [records.tar.gz](records.tar.gz) and extract in a new directory.
`records-inventory.json.gz` lists every included payload's size/hash and every
explicitly omitted state/cache payload. No executable, disc or firmware payload
is included. Original cache files remain local/server-side; this exclusion from
the publication archive does not alter the complete runner collection receipt.

```sh
tar -xzf records.tar.gz
python3 scripts/performance/vk-texture-allocation-summary.py \
  pgr2-on-checkpoint/collected/20261002-063023515-b3b4572c8e90494aa88319ceed4e7c9c/vk-texture-allocations.jsonl
```

Use the reader from tested runtime `8079502ae2` or this documentation-only
publishing commit. Per-procedure manifests, preparation scripts, baked reusable
tests, original assessments, captures, traces, cache qualifications and collection
receipts are included. `allocation-summary.json` and `allocation-classes.json`
provide the strict lifecycle result and disjoint class totals.

Archive: **31,975,347 bytes**, SHA-256
`7042b509f753515d756a3d2d9041b5fe6e658228c58d77ff793d7844b1ef8ac6`.
Packaging rehashed **288 payload files**; **2,173 state/cache files** are explicitly
listed as omitted. [verification.json](verification.json) binds run/build
identities and unchanged original outcomes.

## Remaining gates

The unchanged runtime's CI passed all jobs; focused local collector/reader checks
passed previously. The local full-unit build failure in unchanged resampler
declarations, also reproduced on the parent, remains disclosed in the prior
[shutdown packet](../issue246-allocation-shutdown-20261002/README.md).

No A/A, physical ABBA/BAAB, matched default-off or same-executable OFF/ON overhead
measurement, affected/control per-leaf XISO timing acceptance, eviction/in-flight
reason, upload-byte attribution or heap-pressure qualification is complete.
Transitions and another texture-heavy title remain missing. Resolve the
collector-ON Morrowind risk before treating its data as qualified. Do not infer
pool benefits, promote the draft, or close #246 from this limited window.
