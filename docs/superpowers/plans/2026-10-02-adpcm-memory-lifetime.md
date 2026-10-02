# ADPCM memory lifetime qualification

> **For agentic workers:** Use superpowers:executing-plans for this single task.

**Goal:** Qualify owned RAM retirement and resizing during an ADPCM block.

**Architecture:** Extend the real QEMU memory fixture with a QOM-owned RAM
region. An event-controlled reader stops at its second SGE descriptor while
the BQL owner removes that region or shrinks resizable RAM. Verify payloads
against independent literals and drain RCU before checking owner destruction.

**Tech Stack:** C, GLib TAP, QEMU QOM/memory/RCU, maintained HTTP runner.

**Spec:** Bounded design presented in chat; the user has authorized research
and testing without further questions.

## Global Constraints

- Production ADPCM reader and measured game builds remain byte-identical.
- Native tests use Steam Deck `10.0.0.123` through the maintained HTTP runner.
- Keep failures and mutation-control output; evidence belongs in xemu PR 275.
- Draft/HOLD remains until the broader performance and correctness gates pass.

## Review Focus

- Reclamation must finish before fixture state or finalizer counters disappear.
- Map updates require BQL; readers must register with RCU.
- A shrink must make removed bytes inaccessible despite retained allocation.
- Repeated owner turnover must release all retired owners.
- Existing descriptor/MMIO/decode coverage must still pass in both modes.

### Task 1: Qualify backing lifetime

**Files:** Modify `tests/xbox/mcpx-apu/test-sample-memory.c`; add a report under
`docs/evidence/issue197-memory-lifetime-20261002` and update the canonical PR body.

**Interfaces:** Use `read_block`, the existing descriptor events, QOM-owned
MemoryRegion, `qemu_ram_resize`, and `drain_call_rcu`; add two registered TAP cases.

- [x] Add owned hot-unplug/replacement and resizable-RAM shrink cases, repeated
  64 times each. Check every returned word, callback count and owner finalization.
- [x] Build a separate guard-deleted executable; each new case must fail on
  stale payload data, while its original-reader control passes.
- [x] Run all 13 real-memory cases in original and candidate modes; run the
  configured broader test suite and preserve any existing failures.
- [x] Obtain one fresh review, address material findings, build native artifact
  and run both correctness modes on the Deck using retained HTTP definitions.
- [x] Publish raw output, source/executable identities and applicability limits;
  commit and push tests/evidence, edit PR 275 in place, leave draft/HOLD.
