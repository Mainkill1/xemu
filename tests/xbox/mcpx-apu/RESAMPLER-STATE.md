# Finite voice resampler state fixture

This retained component tool links the actual main VP implementation, QEMU RAM
and AddressSpace, sample reader, resampler callback, notifications, worker
dispatch, and mix stage. It uses one ordinary voice (handle 64) and one worker.
There is no game, DSP program, renderer, device frame thread or emulated clock.

The constructor and read functions are wrapped **only in this executable** to
select sinc or linear and observe the real callback. The emulator's constructor
and default stay unchanged. This is baseline characterization for issue #188,
not the experimental selector implementation: that already exists in PR #189.
PR #189 also has reader and adapter regressions; this tool adds observation of
complete VP frames on its unchanged-main parent.

## Build and run

Use the normal xemu Linux i386 XBOX configuration. For example, from an empty
build directory inside a checkout with the usual build dependencies:

```sh
../configure --target-list=i386-softmmu --extra-cflags=-DXBOX --extra-cxxflags=-DXBOX
ninja test-xbox-mcpx-apu-resampler-state test-xbox-mcpx-apu-resampler-throughput tests/unit/test-xbox-mcpx-apu-resampler
meson test --no-rebuild --print-errorlogs test-xbox-mcpx-apu-resampler-state test-xbox-mcpx-apu-resampler-throughput test-xbox-mcpx-apu-resampler
./test-xbox-mcpx-apu-resampler-state --matrix > state-matrix.jsonl
./test-xbox-mcpx-apu-resampler-state --trace > state-trace.jsonl
./test-xbox-mcpx-apu-resampler-throughput --benchmark sinc mono-adpcm 20000 8
./test-xbox-mcpx-apu-resampler-throughput --benchmark linear mono-adpcm 20000 8
```

The Meson fixture is currently Linux-only because it uses GNU linker wrapping.
`--matrix` emits one row per case and a final summary. `--trace` additionally
emits a begin record, each callback, and each completed VP output block. There
are 600 cases: two converters, four payloads, 15 finite source lengths and five
pitches. Payloads are mono/stereo S16 PCM and mono/stereo IMA ADPCM. Lengths are
1, 2, 7, 31/32/33, 63/64/65, 127/128/129 and 255/256/257 input samples. Pitches
-8192, -4096, 0, 4096 and 8192 correspond to output/input ratios 4, 2, 1, 0.5
and 0.25. This is a bounded pitch sample, not coverage of every supported ratio.

PCM is constant +4096 on the left, with -4096 on stereo right; zero-nibble IMA
blocks independently produce the same values. The callback therefore identifies
real payload versus silence padding without guessing fetched counts from CBO.

## What the values mean

| JSON field | Meaning and unit |
|---|---|
| `input_frames` | Number of source samples per channel; EBO is length minus one. |
| `vp_frame` / `frame_at_off` | One-based VP output block number; each block requests 32 output samples per channel. |
| `callbacks` / `callbacks_at_completion` | Input callback invocations, including EOF/padding callbacks, through the VP frame that completes the voice. |
| `payload_frames` | Nonzero left input samples returned by the real callback; valid as a payload count for this constant fixture only. |
| `generated_frames` | Output samples per channel returned by SRC, including samples the VP subsequently discards or mixes as silence. |
| `mixed_frames` | Nonzero samples in left output bin 0; **not** a general count of all samples mixed for arbitrary audio. |
| `expected_output_frames` / `duration_delta` | Rounded input length times rate, and `mixed_frames` minus that value. Linear's one-frame safety guard can extend output as described below. |
| `mixed_energy` | Sum of squared left-bin sample values, after voice volume and headroom; not decibels. |
| `final_cbo` / `cbo` | Guest source cursor, in samples per channel. Main clamps it to EBO at completion. |
| `active_transitions` | Observed ACTIVE-to-inactive transitions across a completed VP output block. Callback rows separately expose state during fetch. |
| `notify_transitions` | Observed completion status byte transitions, not the number of repeated writes of the same status. |

The ordinary passing suite checks finite termination, cursor bounds, one ACTIVE
and notifier transition, notifier payload, interrupt request bits, channel
relationships, unused bins, and resampler destruction. It records output
duration and energy; it does **not** bless main's finite-tail truncation or assert
sinc/linear waveform or guest-state equivalence. The trace captures main's
completion inside the callback and its subsequent discard before mixing. Its
bounds also allow partial or empty finite EOF callbacks and completion after
mixing, so the same observer can qualify a draining path.

When compiling this fixture against PR #189, add
`-DMCPX_TEST_RESAMPLER_SELECTOR` to this test target's `c_args`. That test-only
compatibility mode sets the existing `audio.vp.resampler` option before VP
initialization and asserts the actual constructor receives the chosen converter.
The full VP implementation and source-drain adapter remain the production
candidate. Compile against that candidate's headers, generated configuration and
objects. `--require-finite-tail --matrix` requires every real source sample and
bounds output duration. Preserve each failed cell.

## Retained negative controls

These commands intentionally fail on unchanged main. Run them separately from
the ordinary Meson suite and preserve their nonzero exits:

```sh
./test-xbox-mcpx-apu-resampler-state --require-finite-tail --tap -p /mcpx-apu/resampler-state/linear/mono-pcm
./test-xbox-mcpx-apu-resampler-state --negative-silence --tap -p /mcpx-apu/resampler-state/linear/mono-pcm
./tests/unit/test-xbox-mcpx-apu-resampler --linear-single-frame --tap -p /mcpx/apu/resampler/linear/streaming-equivalence
```

The first demands complete finite input consumption and bounded audible duration:
main consumes no payload in some one-sample cases. PR #189's linear safety guard
adds up to `ceil(rate)` output samples when the final real block has one sample (source
lengths 1, 33, 65, 129 and 257 here); every other duration stays within one
sample of rounded input length times rate. This bound records a known audio
tradeoff and does not establish native listening acceptance. The second selects
the actual VP monitor path that clears mixbins, catching a tool setup that would
report success while hearing silence. The third is an opt-in library diagnostic
for libsamplerate 0.2.2's known unsafe one-frame linear callback schedule
([upstream #208](https://github.com/libsndfile/libsamplerate/issues/208),
[proposed repair #209](https://github.com/libsndfile/libsamplerate/pull/209)).
The ordinary linear streaming test uses the production callback's padded
32-frame contract. Sinc retains its seven callback chunk sizes. No tolerance was
relaxed to convert the one-frame failure into a pass; the diagnostic is retained.

## Fixed-work throughput tool

`test-xbox-mcpx-apu-resampler-throughput` drives 45 looping voices through the
same production VP frame API. Its constructor-only linker wrapper selects sinc
or linear before measured work; callbacks, sample fetch, worker dispatch and
mixing are production code. It supports mono/stereo ADPCM and mono S16 PCM with
1–16 workers. Each invocation warms 256 VP frames, checks the first measured
frame, then reports elapsed microseconds and a fixed output checksum for the
declared frame count. The timed region contains no tracing or per-frame output.

This is a component benchmark. Use fresh A/A before balanced A/B orders, pin the
same executable and libraries, and retain every attempt. Report baseline time,
candidate time, absolute difference, improvement percentage, ordering and
correctness. It does not replace PGR2 scene-qualified performance, native audio,
XISO timings or hardware accuracy.

## Native runner and evidence

The immutable Deck `.123` job and all artifacts are linked from the owning xemu
evidence report. It uses the maintained HTTP runner's `TargetKind: "process"`;
Mesa, XISO, guest-state and game-scene qualification do not apply to this
component executable. No component pass or process duration is a game speedup.
For reuse, rebuild the executable, pin its SHA-256 and required library hashes,
upload it, and save a new immutable test revision before launch.

Before considering PR #189 ready, qualify its actual draining runtime and PCM,
then use matching native builds/settings with fresh A/A and ABBA/BAAB. Loops,
stream starvation/recovery, reuse, pause, reset, save/load and representative
audio remain separate requirements; the 600 finite cases do not replace them.
