# PTIMER reconciliation: source-level validation

Date: September 29, 2026. Related: #267 and #266.

## Scope and identity

The candidate restores the earlier fork semantic repair over accepted main
`2cbabc7152866dd19fb2c6279c776019f3f5df9a`, retaining upstream's masked-callback
suppression, future-deadline rounding and immediate overdue publication.
It does not restore #81's additional host-schedule cache/state machine.

Candidate production `ptimer.c`:

- Git blob: `cbc8df58e5c64a310b6478a61bc89afcfbb296ab`
- SHA-256: `1ba49c6ee9a496657d5d2380ad78550ffb654bc92114d1068488ec0fb95108c9`
- Fork fixture blob: `2d8aaf24454768f25674229a24a8f30a78e95e38`
- Upstream fixture blob: `1e9b468be444319f9febfb71637fd1816914b529`

The source and maintained fixture functions were compiled locally on Linux
with a small QEMU/GLib API shim. The fork run links the maintained
`ptimer-test-stubs.c`; the upstream run links its maintained
`mock-nv2a-ptimer.c`. The shim supplies the surrounding types/registration and
native unsigned-128 arithmetic helpers. It does not replace `ptimer.c` or
copy its timer algorithm into a favorable demonstration.

**These are source-path tests, not a complete QEMU/Windows build, actual
VMState-stream migration, native IRQ/queue integration, Xbox hardware tests,
or Def Jam gameplay. QEMU's software-wide arithmetic fallback was not tested
by the local native-128 shim.**

## Executed checks

| Check | Result |
|---|---|
| Original reported-source nine-case probe | Six controls passed; ACK, phase and wrapped-zero failures reproduced |
| Reconciled nine-case probe | 9/9 passed |
| Extended maintained fork fixture, GCC 14.2.0, `-O2` | 25/25 passed |
| Independent forward-deadline oracle inside fork fixture | 55,296 cases passed, including first reachable ns and callback progress |
| Retained upstream fixture, GCC 14.2.0, `-O2` | 20/20 passed after four documented phase-correct expectation changes |
| Extended fork fixture, Clang 17.0.0, `-O2` | 25/25 passed |
| Extended fork fixture, GCC AddressSanitizer + UndefinedBehaviorSanitizer, `-O1` | 25/25 passed, no diagnostic; leak checking disabled for process-lifetime fixture allocations |
| Four deliberately regressed candidates | All four failed the intended assertion; correct candidate rerun passed |

Normal local compiles used `-std=c11 -Wall -Wextra -Werror
-Wno-unused-parameter`; sanitized builds also used
`-fsanitize=address,undefined -fno-omit-frame-pointer`.

The negative controls independently remove pre-ACK reconciliation, restore
the implicit zero-target arming heuristic, restore timer-only v4 recovery,
and discard the sampled fractional phases. Respectively, the masked ACK,
wrapped-zero, post-#120 v4 masked restore, and 9 ns first-tick tests abort at
the expected assertion. They are detection controls, not candidate failures
silently excluded from a pass.

## Why four upstream expectations change

At virtual time 1,000,000,100 ns with the fixture's 233,333,333 Hz source,
three ratio-1/1 reschedule controls expect 575,218,740 ns rather than
575,218,741 ns. The denominator-2 control expects 75,218,738 ns rather than
75,218,739 ns. The preceding source-clock fraction has already elapsed.

Independent forward checks:

| Denominator | Absolute deadline ns | Internal ticks one ns earlier | Internal ticks at deadline |
|---|---:|---:|---:|
| 1 | 1,575,218,840 | 367,551,061 | 367,551,062 |
| 2 | 1,075,218,838 | 501,768,788 | 501,768,790 |

The intended next alarm is first reached at the new deadline. All upstream
IRQ, masking, low-zero, multi-epoch and polling assertions remain; no test
was removed. The original expectations were run first and the one-nanosecond
mismatch was retained in the local record.

## Integration and remaining gates

The NVPLL path calls the restored clock-change operation. NV2A pre-load clears
state absent from older streams; v4 recovery accepts either a queued timer
or a nonzero guest alarm; v5 serializes explicit armed state. Existing v4
field order is unchanged. New v5 snapshots cannot be read by v4-only builds.
Already ambiguous zero-target/no-timer v4 state is not claimed recoverable.

Still required before merge:

- Complete exact-head emulator build and native execution of the fork,
  upstream and generic timer suites; compile the real headers/math helpers.
- Real QEMU timer-list/IRQ behavior, actual NVPLL MMIO and old/new snapshot
  streams, including repeated restore and failed-load cleanup.
- Repeated normal-clock Def Jam cold starts and sustained match progression
  in OpenGL and Vulkan, using the previously working build as a reference.
- Current XISO correctness and matched PGR2/Morrowind pacing/resource checks.

No new FPS, latency, power or full-title success is claimed. Keep this PR
draft. Preserve the existing main/baseline and keep #266 open for title
validation. Store further evidence with this emulator PR, not in evidence-only
PRs to the test-suite repository.
