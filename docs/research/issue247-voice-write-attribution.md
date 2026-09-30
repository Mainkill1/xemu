# Issue 247: voice physical-write attribution

This is an opt-in research probe. Every existing `stl_le_phys` remains. The
measured product-code commit is `abd598cf596aff497d3b962bb309221f2d3092cc`,
based on main `2d289cb349bca95eae81b6a54b0f8d965365ff82`.

## Use

Configure with `-Dxemu_apu_voice_write_trace=true`, then set
`XEMU_APU_VOICE_WRITE_TRACE` to a writable CSV filename before launch. The
option defaults to false. With it disabled, the product contains neither
collector symbols nor the phase TLS variable.

The collector records the authoritative old physical word and the computed
new word around the existing store. Atomic counters group attempts, changed
and unchanged words, and nominal RAM address range by register and envelope
phase. There is no shadow voice cache. Phases 0–7 are EF; 8–15 are EA; 16 is
outside envelope stepping. Offset 128 is the unsupported/misaligned bucket.

A mixed counter sequence selects approximately one store in 1024 for timing.
The sampled duration brackets the physical store, includes clock-read cost,
and excludes counter updates. Tiny buckets may have no timed observations.
The fixed sequence is reproducible, but does not provide a random trial or
confidence interval. Frame counts and the existing debug voice active flags
provide exposure; these flags describe processed voice entries, including
paused entries, rather than a guest-memory census taken at frame end.

Normal process exit pauses the APU with the established BQL/APU lock order
before aggregating and freeing counters. Device unrealization removes the
exit notifier and closes an outstanding collector after joining workers.
Forced termination cannot promise a completed trace. The numeric summary row is the
completion marker; the schema header alone is incomplete.

## Initial result

Three Windows PGR2 runs reached the stationary race scene and exited with code
zero. Across their entire launch/navigation/race processes, 88.19–88.23% of
approximately 29.77 million stores wrote an unchanged word. The envelope
count field at offset `0x34` was 99.67% unchanged in the first run. This
justifies further investigation of the equal-write hypothesis. It establishes
no production speedup or permission to skip stores. The admitted Deck run
recorded 88.62% unchanged across its complete process and saved its trace on
normal quit. Its cache controls remain comparison-ineligible. Attribution
timing is excluded from performance acceptance.

## Physical-store audit and remaining gates

The typed physical-store path in `system/memory_ldst.c.inc` translates the
address, dispatches MMIO when applicable, and writes direct RAM before
`invalidate_and_set_dirty`. The latter can invalidate translated code and
mark NV2A/VGA/migration dirty state even for equal values. The generic flat
view write path has a RAM access callback; the typed store used here does not
call that callback. Do not infer callback behavior from the generic helper.

The probe's `ram_range` is only a numeric bound against Xbox RAM size. It does
not certify the translated region, memory attributes, debugger behavior, or
side effects. Dirty/invalidation callback counts and costs are not measured
by this first probe. Equality describes the word read at the start of the
existing read/modify/write operation; it does not prove that a guest writer
cannot change that word before the physical store. Any elision must also
resolve that interleaving and register ownership.

Before an equal-write candidate: prove an ordinary RAM translation guard;
exercise the real helper's dirty, translation and observable side effects;
cover reset/save/load lifecycle; compare PCM and guest state; collect diverse
voice/phase workloads and an audio-light control; and run matched ABBA/BAAB
production comparisons against previous main and the fixed cycle baseline.
No such candidate or acceptance is included here.

Study attribution: the issue's equal-write hypothesis references
`izzy2lost/xemu@e8e92c077a50ab7ec7075a88e0f0014be1071699` (`vp.c`). This probe
was written against the target repository; no foreign implementation source
was opened or copied. Agent: Codex (GPT-6).

## Evidence

[Retained report, original logs and checksums](https://github.com/Mainkill1/xemu-perf-tests/blob/2f1627203c6f38318b5e4c39f1347342675527c4/docs/evidence/research247-20260930/REPORT.md)
contains 12 runner attempts, the Windows ON/OFF/OFF/ON observer check, Deck
scene admission, failed attempts and exact executable identities.
