# Remove unconditional sample-cache clearing

## Located added work

PR #275's combined descriptor/payload cache is 256 bytes in the inspected x86-64 build. `voice_get_samples()` cleared that entire object on every callback, including PCM and streaming callbacks that never use the payload mapping. GCC emitted `mov $32, %ecx; rep stosq` before the sample-format branch. Merely removing `= { 0 }` preserved the same clear because the maintained QEMU build enables `-ftrivial-auto-var-init=zero`.

The earlier PGR2 counter trace recorded 3,745,495 callbacks and 72.55% PCM payload reads, making unconditional callback setup a concrete source of redundant work. Those counts come from PGR2; they do **not** establish the cause of Morrowind's observed 2.31% cadence loss.

## Narrow correction

Use the existing `QEMU_UNINITIALIZED` annotation on this one cache object, explicitly setting the descriptor and payload `mrs.mr` ownership sentinels to NULL. QEMU's cache initialization fills the other fields when each mapping is created. Missing-owner guards short-circuit before inspecting pointer/address/length/FlatView data; destruction also returns on a missing owner. This preserves fresh descriptor and sample reads, reference ownership, mapping changes and fallback paths.

The generated bulk clear is removed: one `rep stosq` in the old sample routine, zero in the new routine. Cache initialization drops from 256 cleared bytes to two pointer stores (16 bytes in this build). Global compiler hardening remains enabled. This is a reduction in setup work, **not a measured FPS percentage**.

## Focused validation

- Strict GCC compile with `-Werror` passes.
- 24/24 production VP fixture cases pass.
- 600/600 resampler-state subcases pass across eight grouped fixture tests.
- 18/18 real-QEMU memory cases pass with unused fields deliberately poisoned in the common reader fixture.
- 18/18 ASan/UBSan cases pass with LeakSanitizer disabled. The sandbox's ptrace restriction prevents leak scanning; its earlier failed leak-scan result is explicitly retained locally.
- Signed commit passes checkpatch with zero errors/warnings.

Both full XISO scopes were cancelled on both rigs, and both queues were verified idle. No additional game benchmarks or input/procedure changes were started. The previous native frame-tail association remains unresolved; these checks do not prove it fixed.

Evidence stays on its dedicated branch outside main. The product change is the narrow commit recorded in the JSON proof on PR #275.
