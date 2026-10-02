# MCPX sample-memory integration test and benchmark

In a Linux production build configured with `-DXBOX` (as by `build.sh`), build `test-xbox-mcpx-apu-real-memory` with Ninja in an existing
xemu build directory. The target links the emulator's system-memory objects
and wraps `main`; it initializes QEMU memory without launching the UI or a
ROM. The ordinary upstream-style unit configuration has no `XBOX` defines
and does not instantiate this full-emulator target. Production Linux CI builds
and executes both controls separately. In production builds it is registered in the `unit` suite, including the original-reader
control. Other platforms retain the smaller API-double unit test.

```sh
ninja -C build test-xbox-mcpx-apu-real-memory
build/test-xbox-mcpx-apu-real-memory --tap
build/test-xbox-mcpx-apu-real-memory --baseline --tap
```

The thirteen real-memory cases run against both readers:

- Nine-word mono and eighteen-word stereo RAM payloads.
- Same-address RAM replacement between the first and second encoded word.
- Valid mono/stereo IMA blocks within a page and across an SGE page boundary.
  Encoded bytes are checked before the production decoder's 65 samples are
  compared with independently calculated ascending/descending ramps.
- Actual MMIO payload reads, checking every access address and exact count.
- Foreign-thread changes to RAM mapping, SGE table base and descriptor page,
  with 256 generations per scenario. QemuEvents stop the reader inside its
  second descriptor callback while the BQL owner publishes the change. The
  reader registers with RCU; the synthetic descriptor uses lockless MMIO to
  avoid acquiring BQL while the owner waits for the event.
- Owned RAM retirement/replacement and resizable RAM shrinking to expose
  MMIO, with 64 generations per scenario. The same descriptor events order
  the changes. Every returned word is checked; the shrink checks all eight
  fallback MMIO accesses. Reader and map-writer RCU drains complete before
  checking that all retired QOM owners have finalized.

Both readers must observe each change at the same word boundary. The table-base
case distinguishes the second word's already-selected descriptor from the
third word's newly selected table. No sleeps establish correctness. These are
event-ordered component checks. They cover legal owned-region retirement and
resize; they do not qualify arbitrary unsynchronized topology changes,
same-map backing-pointer replacement, destruction that violates QEMU ownership,
voice mixing, or native sound output. Owner finalization is checked, rather
than the completion of every subsequent deferred physical RAM allocation free.

For fixed-work measurements, use the **same executable** for both modes:

```sh
build/test-xbox-mcpx-apu-real-memory --benchmark baseline 9 1000000
build/test-xbox-mcpx-apu-real-memory --benchmark candidate 9 1000000
```

Use `18` for stereo. Each mode reads the same RAM descriptor and RAM payload,
cycles through 32 aligned block offsets in one 4 KiB page, validates 4,096
warmup blocks, then reports host elapsed microseconds for the requested block
count. The measured loop sums every output word. Its final checksum must
match an expectation computed independently from the fixture values.
Initialization, warmup and the expected-checksum calculation are outside the
measured interval. Linux release artifacts retain this executable alongside
the AppImage; its dependencies can be supplied from the corresponding
AppImage's extracted libraries.

Run baseline A/A before physical ABBA and BAAB orders. Keep all attempts,
settings, executable/library hashes and output. Report cost improvement as
`100 * (baseline_us - candidate_us) / baseline_us`; positive is better.
A component result measures encoded-word reads, including mapping setup and
release. It does not measure ADPCM decoding, sound correctness, PCM, streaming
voices or whole-game FPS, and cannot replace affected native/XISO coverage.
