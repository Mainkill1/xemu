# MCPX sample-memory integration test and benchmark

On Linux, build `test-xbox-mcpx-apu-real-memory` with Ninja in an existing
xemu build directory. The target links the emulator's system-memory objects
and wraps `main`; it initializes QEMU memory without launching the UI or a
ROM. It is registered in the `unit` suite, including the original-reader
control. Other platforms retain the smaller API-double unit test.

```sh
ninja -C build test-xbox-mcpx-apu-real-memory
build/test-xbox-mcpx-apu-real-memory --tap
build/test-xbox-mcpx-apu-real-memory --baseline --tap
```

The real-memory cases check nine-word mono and eighteen-word stereo payloads,
plus an MMIO SGE descriptor that replaces the RAM backing at the same physical
address between the first and second word. The original reader must see the
new backing for subsequent words, as must the candidate.

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
