<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Voice-store memory contract checks for #247

This retained integration target links the production voice-store helper and
QEMU memory, RAM allocation, dirty tracking, and QOM implementations. It uses
owned 2 MiB RAM regions in the system address space, rather than constructing
a RAMBlock or replacing CPU hooks. Linux x86-64 CI builds, runs, and retains
the executable alongside the voice-frame fixtures.

```sh
ninja -C build test-xbox-mcpx-apu-voice-store-integration
build/test-xbox-mcpx-apu-voice-store-integration --tap
build/pyvenv/bin/meson test -C build --no-rebuild \
  test-xbox-mcpx-apu-voice-store-integration --print-errorlogs
```

The ten TAP cases have independently specified expected words and observers:

| Case | Contract checked |
| --- | --- |
| `changed-masks` | Byte fields, full words, NEW_VOICE, ACTIVE_VOICE, and CBO updates produce literal expected words and dirty all three NV2A clients. |
| `equal-clients` | Admitted equal writes preserve RAM and dirty NV2A, NV2A_TEX, and NV2A_SURFACE just like physical stores. |
| `fallback-guards` | Equal low-address and unaligned writes use the physical fallback and reach its dirty observers. |
| `alias-remap` | A remap to another offset after the read uses the new mapping and revalidates the complete word. |
| `other-ram-owner` | Equal contents in a different RAM owner cannot admit the shortcut. |
| `mmio-remap` | A RAM-to-MMIO remap preserves equal and changed physical writes observed by the MMIO callback. |
| `ram-resize` | Shrink exposes an MMIO fallback; regrowth admits the current RAM word without retaining the old extent. |
| `ordered-writers` | 10,000 writes before validation and 10,000 writes after completion preserve the original masked read/modify/write result and subsequent writer result. Only bits outside the selected mask change. |
| `equal-surface-ownership` | A same-value write supersedes a pending GPU readback through the real dirty-page bitmap and production surface ownership helpers. |
| `owner-retirement` | 64 legal unmap/unref/RCU-drain cycles finalize each owner and use the next owner's independent contents. |

The original nine cases observe 73 owner finalizations; the added ownership case retires one more owner. It does not keep a
MemoryRegion alive through an artificial pointer replacement. Mapping changes
occur under BQL; retired owners have been unmapped, and pending RCU callbacks
are drained before checking finalization. The writer uses QemuEvent handshakes
to establish ordering, with no sleep-based correctness condition.

## Scope and remaining contract

An equal masked result still requires validation of the **entire** word.
When another field changes before validation, the original helper's physical
fallback writes the result computed from the original read. The test checks
that existing behavior, rather than inventing a merge with newer fields.

RAM byte equality does not make all dirty notifications redundant. The APU
and NV2A share the same system RAM owner. A cached GPU surface can hold newer
contents than RAM; an equal physical store must still transfer ownership back
to guest RAM and cancel a stale GPU readback. The helper now invokes
`memory_region_invalidate_and_set_dirty()` after a successful CAS, reusing the
physical store's dirty-client filtering and translated-code invalidation. The
real-AddressSpace unit fixture also compares the CODE invalidation hook with
the physical-store reference. Substituting `memory_region_set_dirty()` is not
equivalent: it marks CODE dirty without invalidating translations.

The remaining writer contract is word-level: a successful full-word CAS is
the equal store's linearization point; a mismatch keeps the original stale
read/modify/write physical fallback. It does not merge newer fields. RCU
protects the resolved mapping while the operation and observer notification
complete. No descriptor lock, cached mapping or cross-voice ordering is added.

These event-ordered C accesses do not establish safety for unsynchronized
guest/vCPU stores executing concurrently with the helper. The fork's
[atomic rules](../../../docs/devel/atomics.rst),
[memory ownership rules](../../../docs/devel/memory.rst), and
[MTTCG implementation](../../../docs/devel/multi-thread-tcg.rst)
remain the relevant contracts. RCU protects mapping lifetime; it does not
serialize every RAM writer. The corresponding upstream explanations are
[atomics](https://www.qemu.org/docs/master/devel/atomics.html),
[memory](https://www.qemu.org/docs/master/devel/memory.html), and
[MTTCG](https://www.qemu.org/docs/master/devel/multi-thread-tcg.html).

The tests also do not qualify live APU reset/save-load, audio capture, execution of live TCG translations, Xen, migration/debugger sessions, or guest XISO
correctness and timing. Passing them narrows mapping and dirty-client risks;
it does not remove the draft PR's remaining gates. Test duration is not a
performance metric. The separate voice-frame target measures fixed VP work.

## Negative controls

The linked #247 contract evidence retains a mutation driver that compiles
separate helper objects and links them with these actual production objects.
It changes no production source or normal executable. Five executions must
reject four defective alternatives:

| Mutation | Rejecting case |
| --- | --- |
| Return on read-value equality | `ordered-writers`, `mmio-remap` |
| Validate only selected bits | `ordered-writers` |
| Admit any RAM owner | `other-ram-owner` |
| Drop the changed physical store | `changed-masks` |
| Omit equal-write dirty notification | `equal-surface-ownership` |
| Replace physical notification with `memory_region_set_dirty()` | real-AddressSpace CODE invalidation check |

Mutation assertions, build/exit logs, executable identities, and native Deck
TAP output belong in the owning xemu PR's evidence packet.
