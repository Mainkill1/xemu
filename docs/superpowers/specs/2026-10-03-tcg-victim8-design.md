# Issue #167: eight-entry miss-only cache experiment

## Intent and scope

Find a useful uninstrumented performance improvement on Steam Deck `.123`.
The reviewed PGR2 diagnostic found 5,620,784 potential eight-entry recoveries,
20.40% of validated global hits; this is motivation, not a predicted speedup.
Keep one fixed candidate, correctness controls, retained tools and readable
baseline/candidate evidence. All PRs remain draft; never merge.

This is an architectural cache ownership change across dispatch and
invalidation. The user's standing research/design autoapproval applies;
execute locally without more approval questions or delegated implementation.

## Selected design and alternatives

Append eight owner-private `(TB pointer, virtual PC)` FIFO entries after the
existing 4,096-entry primary array. Consult them only after the unchanged
primary hit check. Validate virtual PC, CS base, flags and exact cflags,
including `CF_INVALID`. Promote a match into the original primary slot and
retain its valid displaced entry. Otherwise use the existing physical hash
lookup and code generation. Cover both global-hit and generated fills.

A two-way cache changes the common hit path and generated layout; a larger
direct map increases primary storage and clear work. They remain alternative
issue experiments, not bundled into this first candidate. A generation-only
protocol cannot identify an ongoing or overlapping foreign clear, so use a
generation plus active invalidator count. Do not add probe counters or clocks.

## Ownership, invalidation and lifetime

Only the owning, serialized CPU accesses victim entries, count and owner
generation. Foreign invalidators update only atomic generation/active count
and the existing atomic primary pointers. Invalidation increments active
count then generation before primary clearing; after clearing it increments
generation then decrements active count. Overlapping invalidators are counted
independently. No invalidator reads or writes owner-private victim fields.

A miss captures the generation and discards private history when generation
changed or a writer is active. Validate stable generation and zero writers
before dereferencing a remembered pointer. Reader acquire barriers order key
checks against invalidation. Promotion/fill publishes the primary pointer,
uses a full store/load barrier, then checks generation and writers again.
If publication overlapped invalidation, discard history and clear that primary
slot; victim lookup falls back to the real global lookup. Global/generated TB
results preserve their existing return path even when not retained.

Bracket whole, page/range and targeted physical clears. Targeted invalidation
marks victim history even when the invalid TB is absent from its primary slot.
Keep current/preceding-page clearing and primary hash/page grouping unchanged.
Every full flush/reset/load path continues through `tcg_flush_jmp_cache`.
All TB storage reclamation remains in the existing exclusive/serial full-flush
protocol; no CPU may execute during reclamation. Generation checking does not
replace that lifetime guarantee or physical code invalidation.

## Interface and build

`tcg_victim_cache_epoch(jc)` returns a generation token and synchronizes owner
history. `tcg_victim_cache_lookup(jc, hash, state, epoch)` validates and promotes
a victim or returns NULL. `tcg_victim_cache_fill(jc, hash, pc, tb, epoch)` performs
the primary fill and optional victim retention, returning whether its
publication stayed valid. `tcg_victim_cache_invalidate_begin/end(jc)` bracket
primary clearing, accept NULL during early initialization, and become empty
inline functions in compile-disabled builds.

Build option `xemu_tcg_victim_cache` defaults false. Enabled miss helpers live
in `accel/tcg/tb-victim-cache.c`; shared structures and declarations live in
`accel/tcg/tb-victim-cache.h` and `tb-jmp-cache.h`. Preserve primary offsets.
Expected additional allocation is 160 bytes on x86-64; report measured size.
No runtime activation flag, instrumentation, adaptive resizing or hash change.

## Qualification and decision

Retain tests using production hash, real TB/state structures and actual helper
implementation. Prove the original direct map loses the deliberately colliding
entry before implementing recovery. Check full key/CF_INVALID, FIFO capacity,
mapping invalidation while the TB remains valid, second-page invalidation,
overlapping invalidators, delayed publication, guarded stale storage and reuse.
Audit all production fill/clear hooks. Unit controls alone do not qualify VM
mapping, reset/load or full concurrent execution; retain that limitation.

Build matched default-off/on binaries with GCC14/O2. Check offsets, allocation,
symbol absence in the disabled build and compiler code shape versus true
parent. Use maintained LAN HTTP and immutable XISO/reference identities.
Explicitly select suite configuration for binary-only experiments. A/A precedes
physical ABBA and BAAB. Preserve every failure and all samples; no reference
selfapproval, cache waivers, global purges or silent reruns. Useful additional
XISO fixtures require their own retained source draft PR.

Publish code plus absolute baseline/candidate time, time saved and Improvement
% (positive means better) with both orders in the owning xemu draft PR. A
promising candidate requires broader XISO and reached retail coverage, at
least 300 seconds for PGR2, before readiness. Stop this variant on adequate
negative performance evidence or unresolved correctness; do not infer gain
from recoverability counts or build/test success.
