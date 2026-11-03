Experimental DSP-bypass audio
=============================

The advanced Audio engine selector is a development prototype. Default
preserves the existing realtime-DSP and JIT settings. JIT selects realtime
GP and EP execution with the JIT backend. HLE selects an experimental path
that skips the GP/EP frame function and routes the VP's front-left and
front-right mixbins to the existing stereo monitor. Selection is latched at
device realization; restart after changing it. Existing DSP settings remain
intact when switching back to Default.

The HLE path does not run GP or EP instructions during frame processing.
Guest MMIO and bootstrap still access DSP state, and the emulated DSP objects
remain allocated. The direct mix uses per-bin voice gains rather than the
legacy VP monitor's maximum-voice-gain heuristic. It does not implement DSP
effects, reverb, arbitrary mixbin routing, additional output channels,
DSP-generated output, DMA, interrupts or other guest-observable DSP effects.
Consequently guest programs that rely on those effects can stall or produce
incorrect audio. Save/restore across different engine selections is not
qualified. No game compatibility, audio quality or performance claim follows
from the initial synthetic voice test.

The separate public fixture in ``xemu-perf-tests/tools/hle-audio-proof``
generates two independently checkable stereo tones. Its GP and EP programs
write sentinel words only when they execute. The host captures the emulator's
actual SDL output through an isolated Pulse sink, validates frequencies,
channel isolation, level, clipping and continuity, and verifies both DSP
sentinels. A controlled GP-only variant applies asymmetric channel gain and
permits a DSP JIT versus native front-mix comparison. See that fixture's
README for commands, identities, expected failures and limitations.

With the JIT engine, ``XEMU_GP_DECIMATE_SHADOW=1`` enables a read-only
diagnostic for four decimation routes of one identified GP program. It
calculates private predictions before running the original DSP and compares
their samples, scratch writes, descriptor offsets and phases afterward. It
prints frame, route and nonzero matched-route counts periodically and on
normal device teardown. Unknown programs, unsupported states and changed
programs are excluded and counted; HLE audio
does not consume these predictions. This diagnostic does not establish Xbox
hardware accuracy, complete game audio, or a performance improvement.

``XEMU_GP_FILTER_FRAME_SHADOW=1`` separately compares eight candidate
two-section filter outputs and their histories with the same identified GP
program after the JIT frame completes. It counts matched channels with
nonzero output separately. This opt-in diagnostic also leaves the DSP in
control and makes no claim about the rest of the GP or EP program.

``XEMU_EP_MIX_SHADOW=1`` enables a separate EP diagnostic for one identified
program and loaded overlay. It collects the observed scratch inputs and the
FIFO output into private storage and compares a guarded five-channel stereo
prediction with the DSP output and the input/mixed XRAM buffers. Unknown
program/overlay identities, changed
programs and unexpected I/O sequences are excluded. It counts matched and
nonzero frames and individual matching/mismatching samples. The prediction
is not used for playback and does not cover other EP state or side effects.

The intended future implementation must cover real guest DSP programs and
their state, effects, DMA and IRQ behavior. Use independently verified program
contracts and shadow comparisons; do not treat this direct-path prototype as
a full HLE implementation. OpenAL loopback remains an optional candidate for
specific effects only after their parameters, state and output match an
independent reference and their real cost is measured.
