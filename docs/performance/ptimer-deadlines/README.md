# PTIMER future deadlines

Current qualification: [native stream results and failures](NATIVE-STREAMS.md).
Real v4/v5 streams loaded and the native unit passed; gameplay qualification
failed. PR48 remains draft. Earlier reports below retain their original scope.

Latest implementation: [masked alarms and PLL transitions](MASKED-ALARMS.md),
with its own source identity and 23-test results. Real stream/native qualification
remains pending. Earlier reports below retain their original scope and identities.

Current follow-up: [acknowledgment and stopped-clock reconciliation](RECONCILIATION.md).
The arithmetic results below qualify the earlier identified implementation;
the follow-up records its own source identity and 19-test result.

Issue #39 concerns device deadline arithmetic, independently of the Windows host
wait and masked-alarm scheduling in #40. The parent rounds both inverse clock
conversions down and adds the result to a later clock sample. Positive alarm
distances can produce early or zero deadlines; large distances can also overflow.

The repair samples virtual time once, retains the remainders from both forward
clock conversions, and inverts each stage with upward rounding. It retains the
existing counter values, time offset, register wrap, IRQ and acknowledgment
behavior. Products and intermediate quotients stay wide until their range is
checked. Deadlines beyond the signed virtual-clock horizon saturate to `INT64_MAX`.

The existing forward model also wraps its intermediate GPU counter at 64 bits.
When that discontinuity precedes a distant alarm, the repair schedules a
reconciliation at the wrap rather than extrapolating through it. This boundary
callback need not assert an alarm; the existing alarm predicate decides that.

## Source and scope

Test parent: `d9bf8e1dcfd4ee72813c9f9f5572b780fc65cc76`.
The PR is reconciled onto integration parent
`bac2cae991a2d359eddfc6836dec3db60fba5618`, which adds the separately reviewed
LRU repair. PTIMER production and test inputs remain unchanged by that merge.
Tested implementation: `1dade3e92678b70d9114a3756b51f7e7d4007c41`.
Only `hw/xbox/nv2a/ptimer.c` and its existing translation-unit regression test
change in that implementation commit. The production file is compiled directly;
a minimal NV2A shim and controlled QEMU clock/timer stubs supply its environment.
The masked-alarm and shared stub cleanup work remains separate.

For positive distance `d`, source frequency `F`, ratio numerator `N`, denominator
`D`, and remainders `rg` and `rp` at the captured time:

```text
G = floor(now * F / 1e9), rg = (now * F) mod 1e9
P = floor(G * D / N),    rp = (G * D) mod N
required GPU ticks = ceil((d * N - rp) / D)
remaining ns       = ceil((required GPU ticks * 1e9 - rg) / F)
```

The counter-wrap and signed-horizon checks bound the resulting deadline. The
forward guest clock is not flattened into a different single-ratio conversion.

## Results

| Deterministic control | Parent | Repair |
| --- | ---: | ---: |
| 233,333,333 Hz, ratio 1:1, first internal tick | 4 ns | 5 ns |
| 233,333,333 Hz, numerator 1 / denominator 2, first tick | 0 ns | 5 ns |
| 100 MHz, ratio 1:1, sample at 9 ns, first tick | 19 ns | 10 ns |
| Beyond signed deadline horizon | 0 ns after overflow | `INT64_MAX` |

The same production-unit test source fails each of those four controls against
the parent's PTIMER source. Both compiler-supported 128-bit arithmetic and QEMU's
software wide-arithmetic helpers pass all **16 tests** under Wine. Each run
includes **34,560 forward-clock oracle cases**: three frequencies, numerator and
denominator 1–6, five initial phases, and 64 positive distances. The oracle walks
integer nanoseconds forward independently of the inverse helper and checks the
first time the requested tick is reached. Each case also dispatches the actual
alarm callback and verifies that its next deadline advances.

Additional tests retain existing IRQ/acknowledgment, overdue-epoch and restore
coverage, and exercise both fractional phases, zero ratios/source frequency,
full register wrap with an offset, wide intermediate values, source-counter
wrap, and signed deadline saturation.

These are deterministic Windows-target unit executions under Wine, **not native
Windows emulator qualification or host wake-latency measurements**. No retail or
XISO campaign was run. No FPS, p95/p99, CPU, power, or end-to-end speedup is claimed.
The exact integer results address correctness; scheduling-cost and game-level
qualification remain pending. No ARM runtime qualification is implied by testing
the software arithmetic helpers on x86-64.

## Reproduction and manifest

The existing Meson `test-xbox-nv2a-ptimer` target compiles the production source
with `xbox-nv2a-ptimer-test-shim.h` and `ptimer-test-stubs.c`. In a configured build,
run that named unit target. For the negative controls, compile the same updated
test against the parent's `ptimer.c`, then select these GLib test paths with `-p`:

```text
/xbox/nv2a/ptimer/fractional-deadline/ratio-1-1
/xbox/nv2a/ptimer/fractional-deadline/ratio-1-2
/xbox/nv2a/ptimer/deadline-phase
/xbox/nv2a/ptimer/deadline-range
```

The recorded run used a standalone Windows cross-link of those three sources
with GLib, optimization `-O2`, and generated Windows build configuration. The
software variant undefines `CONFIG_INT128` after including `qemu/osdep.h`, adds
`util/host-utils.c` and `util/int128.c`, and uses the same tests. The full emulator
and Meson build were not rebuilt for this experiment. Source and unit-binary
identities are in [manifest.json](manifest.json); results are in [results.csv](results.csv).
