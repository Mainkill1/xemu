# PTIMER acknowledgment and stopped-clock reconciliation

Tested implementation: `8f85f6b3edd5ef3ecd4b8305af818efc4bc197f9`. Parent: `d29e1ca90642d46d33a14df69bd99344e20f2639`.

An elapsed alarm can remain unmaterialized when the guest acknowledges it before
its callback runs. Previously the acknowledgment cleared only the pending bit;
unmasking then latched the acknowledged epoch again. The repair first reconciles
elapsed state, advances the alarm epoch, then applies write-one-to-clear and
publishes the resulting IRQ state.

The shared latch helper now also requires a running clock. Reading interrupt
status or restoring an armed zero comparator while the ratio/source is stopped
must not manufacture a new pending interrupt. Already-restored pending state is
preserved and still asserts IRQ when enabled.

| Production-unit regression | Parent | Repair |
| --- | --- | --- |
| Masked elapsed alarm → acknowledge → unmask | Incorrect pending IRQ | No IRQ until next epoch |
| Stopped clock → read status | Newly latched pending bit | Pending remains clear |
| Stopped clock → restore | Newly latched pending bit | Clear stays clear; existing pending preserved |

All three tests failed on the parent with `pending_interrupts == 0` observing
`0x1`. After the fix, **19/19 tests passed**, including the existing 34,560-case
forward-clock oracle. Execution used the production PTIMER translation unit,
Windows-target `-O2` compilation with compiler 128-bit arithmetic, and Wine.
The toolchain/configuration match [the earlier manifest](manifest.json).
The portable-arithmetic variant was not rerun for this state-only change.

Select controls with the existing unit binary's GLib `-p` argument:

```text
/xbox/nv2a/ptimer/masked-ack
/xbox/nv2a/ptimer/stopped-read
/xbox/nv2a/ptimer/stopped-restore
```

This completes two prerequisites for issue #40, **not masked callback retirement**.
The host timer still represents the armed state. Explicit guest armed state,
backward-compatible snapshot restoration, clock-change reconciliation, and mask
cancellation remain pending. No emulator, retail, XISO, latency, CPU, or power
qualification is claimed. Existing performance results do not qualify this head.
