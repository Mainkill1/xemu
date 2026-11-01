# Production voice-frame comparison for #247

The Linux XBOX targets link the actual VP, ADPCM decoder, libsamplerate and
QEMU RAM implementations. This is the retained #197 voice-frame fixture
adapted to provide the ordinary system-RAM owner required by #247. It does
not combine the #197 sample reader optimization with the equal-store patch.

```sh
ninja -C build test-xbox-mcpx-apu-voice-frame test-xbox-mcpx-apu-voice-frame-stores
build/test-xbox-mcpx-apu-voice-frame --tap
build/test-xbox-mcpx-apu-voice-frame-stores --tap
build/test-xbox-mcpx-apu-voice-frame --benchmark mono-adpcm 20000 8
build/test-xbox-mcpx-apu-voice-frame-stores --benchmark mono-adpcm 20000 8
```

## Work and independent oracle

RAM is 2 MiB mapped at physical zero, with the APU RAM owner and host pointer
initialized as in `mcpx_apu_init`. Voice data starts at 1 MiB. All three dirty
clients used by `nv2a_init` are enabled on that RAM. No renderer consumes or
clears their bitmaps; no vCPU or TCG code executes. This models stable VP work
with enabled NV2A dirty tracking, not concurrent graphics/guest processing.

Forty-five nonstreaming 2D voices use a 4096-sample loop, unity pitch/sustain,
no filtering, two audible sends and headroom divisor 64. Mono ADPCM encodes
constant +4096; stereo encodes +4096/-4096; mono PCM contains +4096. Logical
SGE pages map to separated physical pages with poisoned gaps. Six TAP cases
cover the three profiles with one and eight production worker threads.

After 256 warmup frames and one qualified frame, run an exact number of
32-sample frames. Each timed frame checks all bins against independently
expected +/-45/512 mixes. Its checksum contributes 184320 at 16-bit sample
resolution, and a tighter error bound permits at most 32 signed-24-bit units.
Final checks require 45 dispatched and active voices, independently expected
buffer offsets, untouched gaps and completed worker/SRC finalization. Outside
the timed interval, a separate libsamplerate instance counts consumption of
32-sample input chunks at unity rate, including sinc lookahead. Its sample
count modulo 4096 is the expected offset for every voice; it never reads VP
state or calls the production store helper. This detects uniformly dropped
changed offset writes even when the constant mix still matches. The constant payload
cannot prove sample ordering; separate correctness cases must cover it.
`--negative-silence` selects the production VP-monitor path and must fail.

## Separate counts and timings

The ordinary executable reports host monotonic time including the output
oracle. It contains no store-count probe. The `-stores` executable uses a
linker wrapper around the real `address_space_stl_le` implementation and
forwards every store. During measured frames only, it classifies physical
voice-table stores as changed or unchanged from the actual RAM word.
Counters are per voice, updated by its owning worker and aggregated after
frame dispatch completes. Both executables use the same fixture source.

The diagnostic JSON adds `diagnostic_physical_stores` and
`diagnostic_unchanged_physical_stores`. Its elapsed time is explicitly
**diagnostic-only / NOT COMPARABLE**. Subtract candidate physical stores from
original physical stores only after equal work, final state and output have
been verified. This is not an attempted-store counter in the candidate.

Build identical fixtures against the fixed original and candidate with
matching compiler options and all corresponding runtime libraries. Retain
both binaries and all declared build slots in saved runner tests before
physical original A/A controls, ABBA and BAAB. Report all profiles and worker
counts, controls, absolute time saved and `100*(original-candidate)/original`.
Keep diagnostic count runs separate from timing cohorts.

The fixture does not prove a game FPS improvement, full DSP/audio pacing,
audible playback, guest direct-writer ordering, notifications, reset/save-load,
or XISO/hardware correctness. Those issue #247 gates remain separate.
