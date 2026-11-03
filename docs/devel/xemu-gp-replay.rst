.. SPDX-License-Identifier: GPL-2.0-or-later

DSP frame replay for audio development
======================================

The optional recorder captures one GP or EP JIT invocation, its native state and
ordered scratch/FIFO transfers. It provides a DSP-level oracle for native
implementation work. A matching replay does not establish hardware accuracy,
whole-APU ring/scheduling parity, audible equivalence or a game speedup.

Build the emulator and developer replay executable::

  ninja -C build qemu-system-i386 test-xbox-mcpx-apu-dsp-replay

Enable full DSP processing and the JIT in the normal configuration::

  [audio]
  use_dsp = true
  use_dsp_jit = true

Set these environment variables on the usual emulator launch::

  XEMU_GP_REPLAY_PATH=/absolute/private/new-frame.bin
  XEMU_GP_REPLAY_DELAY_MS=180000

For the EP instead, use ``XEMU_EP_REPLAY_PATH`` and
``XEMU_EP_REPLAY_DELAY_MS``. Separate fresh paths may be configured to
capture both processors during the same run.

The delay is host milliseconds after DSP initialization, in the range 0 through
86400000; its default is zero. The first subsequent invocation of the selected
processor is recorded. EP executes every eighth audio frame when enabled.
There is one attempt per configured instance. An absent path disables recording.
Output creation is exclusive, with mode 0600: use a fresh path. Capture errors
are reported to stderr. Recordings contain game program, state and audio data;
retain them as private test inputs rather than committing them to source.

After capture, verify it in a separate process::

  build/test-xbox-mcpx-apu-dsp-replay --replay /absolute/private/new-frame.bin

The command prints JSON with ``processor``, ``matched``, ``calls`` and
``transactions`` and returns zero only when the recorded transfer sequence, write bytes and native
post-state match. Read payloads are injected into a fresh real JIT/DMA instance.
Replay verifies the recording itself; bind its hash to the producing executable,
inputs, settings and runner in the owning experiment's manifest.

Version 1 uses a separate VMState field codec for the dsp56300 0.1.3 native
interface. The file magic identifies GP or EP; existing GP recordings retain
their original magic and remain readable. It preserves all 128 interrupt
slots, wide vector/PC fields, power
state, registers, stack, RAM and common DMA state. It does not use the legacy
APU migration layout or serialize host pointers or C padding. Limits are 4096
transfers, 1 MiB of payload, 65536 calls and a 4 MiB input file. Overflow or
incomplete output is rejected; original emulation still receives every transfer.
Replay executes the captured program in the existing JIT. These format bounds
do not bound execution time or sandbox malformed programs; replay only trusted
developer recordings.

The capture runs on the APU thread and temporarily forwards through the existing
callbacks. It adds copying and file I/O to that diagnostic invocation. Restore
and replay run separately; recording is not a performance comparison. Matching
initial/final program RAM does not prove absence of intermediate program writes
or overlays, so HLE recognition still needs explicit invalidation/fallback rules.
