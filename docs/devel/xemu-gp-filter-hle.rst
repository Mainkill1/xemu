.. SPDX-License-Identifier: GPL-2.0-or-later

Experimental native GP filters
==============================

The optional GP filter backend processes recognized two-section fixed-point
filters natively at existing DSP call boundaries. Remaining GP work, EP work,
unknown programs, unsupported state and short loop remainders use the JIT.
It is disabled by default while game, PCM and performance qualification remains
incomplete. This is not a replacement for all programmable DSP processing.

For developer testing, enable full DSP/JIT in the configuration::

  [audio]
  use_dsp = true
  use_dsp_jit = true

Set ``XEMU_GP_FILTER_HLE=1`` when launching xemu. Other values leave it disabled.
The build must identify dsp56300 0.1.3; other or unknown library versions retain
the JIT even when the environment variable is set.
Selection requires the entire normalized 4096-word program identity, currently::

  f7c1b74d7ff1493a3c52fd2ca032fe744d96fabf56b6dd93de22114c793e525b

Identity is checked against all program RAM before each native use, including
changes made by DSP instructions. Runtime coefficients, history, modes,
addresses and loop state are validated separately. Reset, restore, engine
changes and program-cache invalidation require revalidation. One ordinary JIT
call after such transitions synchronizes provider control state that is outside
the native FFI state image.

The kernel preserves the supported dsp56300 0.1.3 accumulator, status,
register, history, output and cycle-budget contract. It stages a complete call
before committing any CPU or RAM changes; a rejected call runs on the JIT.
The final hardware-loop iteration and stack restoration remain on the JIT.
No game program words are embedded in the source.

Enable ``mcpx_apu_gp_filter_hle`` tracing to verify actual native coverage.
Events report starting PC, samples and emulated cycles processed, not host time
or speedup. Capture/replay can also compare complete state and ordered external
transfers; see ``xemu-gp-replay.rst``. A matching replay by itself does not prove
that native processing occurred: unknown inputs may fall back throughout.
The ``test-xbox-mcpx-apu-gp-filter-hle`` fixture also accepts
``XEMU_GP_FILTER_HLE_TEST_CAPTURE=/private/gp-frame.bin``. Its optional captured
frame test requires both complete replay agreement and fewer provider run calls;
the test is skipped when no capture is supplied. Use a recognized capture and
the supported provider for this positive-path check.

Native processing and replay agreement do not establish hardware accuracy,
audible parity, whole-APU scheduling parity or an overall game benefit. Keep
captures private and bind evidence to exact executable, program, settings and
workload identities before drawing correctness or performance conclusions.
Hardware parity still needs an independently authored DSP test XBE for signed
impulses, history, saturation and loop-tail behavior on Xbox and xemu.
