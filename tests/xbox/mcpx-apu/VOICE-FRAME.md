# Fixed-work production voice-frame benchmark

Linux `XBOX` builds retain `test-xbox-mcpx-apu-voice-frame`. It wraps `main`
and links the actual production VP, decoder, libsamplerate and QEMU memory
implementations. The caller owns a stable synthetic RAM region and an APU
state initialized for the public VP interface; it invokes no device frame
thread, timer throttle, GP/EP DSP program, guest CPU, renderer or sound stream.

```sh
ninja -C build test-xbox-mcpx-apu-voice-frame
build/test-xbox-mcpx-apu-voice-frame --tap
build/test-xbox-mcpx-apu-voice-frame --benchmark mono-adpcm 20000 1
build/test-xbox-mcpx-apu-voice-frame --benchmark stereo-adpcm 20000 8
build/test-xbox-mcpx-apu-voice-frame --benchmark mono-pcm 20000 8
```

Each profile has 45 nonstreaming 2D voices, handles 64–108, unity pitch and
sustain, no filtering, a 4096-sample loop, and two audible mix sends. The
headroom divisor is 64. Constant zero-nibble IMA blocks encode predictors
`+4096/-4096`; mono PCM contains `+4096`. Independently, each steady frame
therefore mixes `45/512` into bin 0 and `+45/512` or `-45/512` into bin 1.
Three logical SGE pages map to separated physical pages; gaps remain poisoned.
The 45-voice count models the affected voice count in the retained PGR2 snapshot,
not its sample contents, DSP program or game workload.

The fixture runs 256 warmup frames plus one output-qualified frame, then a
fixed number of 32-sample frames. It measures host monotonic elapsed time
around the loop. Every timed output sample is checked through a maximum-error
accumulator and a checksum: each frame contributes exactly `184320` at 16-bit
sample resolution; a separate 32-unit signed-24-bit error bound is tighter
than that checksum's rounding resolution. Other bins must stay zero. These
validation costs are included in both builds' measured loop. Final checks
require 45 dispatched voices, all voices active with identical current-buffer
offsets, untouched physical gaps, and safe worker/resampler finalization.

Six fixed-work TAP cases cover all three profiles with one and eight production
worker threads. Three additional cases exercise the production VP method and
RAM paths at callback boundaries. They verify a locked format update before a
frame, an update that waits behind an active frame, a direct descriptor update
between frames, reset, and a voice-table base change. The checks compare mix
output, current-buffer offset, active state, and headroom with independently
initialized mono and stereo PCM references.

`--negative-silence` deliberately selects the VP-monitor path, which clears the
ordinary mix bins: the same oracle must abort rather than produce a passing
benchmark. It is a test-executable option, not a production switch.

Build the **identical fixture** against the fixed original and candidate
source, using identical compiler options and runtime libraries. Retain source,
executable/library hashes, all attempts and negative controls. Run original
A/A controls, then physical ABBA and BAAB orders on the maintained HTTP runner.
Report `100*(original_us-candidate_us)/original_us`; positive means less time.
Report every profile and both worker counts; all three profiles execute the
voice-format reads measured by the benchmark.

This measures fixed VP frame work including translation, decoding, sinc SRC,
mixing and worker dispatch. It does not measure whole-emulator/game FPS, timer
pacing, audible output, full DSP processing, guest XISO timings or hardware
accuracy. The timer-paced guest fixture remains correctness-only. No passing
test or benchmark alone qualifies the emulator PR for merge.
