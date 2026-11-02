.. SPDX-License-Identifier: GPL-2.0-or-later

Experimental DSP/native shadow contract
=======================================

The first shadow boundary is the public synthetic program in
Mainkill1/xemu-perf-tests PR 69. Its full normalized 4096-word P-image SHA-256
is 41a14ad0eff5ce00e45fb73c81f497d1d1056bf1ffc6301b91b1d78d5663778a.
The program starts at PC zero, reads 64 signed 24-bit X words at $100,
writes 64 words for each gain +1/2, -1/2 and +1/4 at $200/$240/$280,
then exports those words through scratch DMA. All divisions round down.

For the initial implementation the emulated DSP is always authoritative.
After exact program/mode admission, copy the input words into a private job
and run native arithmetic on a precreated worker while the original DSP
frame runs. Join the private job at the frame boundary and compare all
enabled route output words against DSP X RAM. Never execute candidate DMA,
write native output to the live DSP, or change a guest-visible register.

Expose an explicit route mask for gradual shadow qualification. Disabled
routes are counted as LLE-only/fallback; unrecognized programs are counted
separately. A deliberately corrupted candidate sample must produce a
mismatch while the guest's DSP outputs still pass their original checks.
Stats must show eligible jobs, actual native calls, compared/matching/different
words, fallback counts and the first difference per route. Zero comparisons
must not report a match. Diagnostic native wall time is not game speedup;
the whole DSP-frame time is not a matching per-kernel reference measurement.

This is one stateless arithmetic boundary, not a general DSP replacement.
DSP internal state, cycles, side-effect order, persistent effect tails and
safe HLE-to-LLE state transfer still require routine-specific contracts.
OpenAL research remains a separate loopback candidate experiment; library
effects/resamplers must not be treated as bit-exact Xbox behavior by name.

Controls and evidence
---------------------

Build with ``-Dxemu_dsp_capture=true`` and set ``XEMU_DSP_GAIN_SHADOW=1``.
The worker, synchronization objects, checksum and checkpoint storage are
created before the APU thread. There is no shadow hook in a recorder-disabled
build. Ordinary execution remains the default even in diagnostic builds.

``XEMU_DSP_GAIN_SHADOW_ROUTES`` is a decimal bit mask 0..7 (default 7): bits
0/1/2 select half gain, inverted half gain and quarter gain. A selected route
is always a candidate, never a serving native path. Mask zero deliberately
exercises all-fallback coverage and reports ``NO_COMPARISONS``.
``XEMU_DSP_GAIN_SHADOW_FAULT`` is an optional flattened word index 0..191;
it flips one candidate bit for detection testing and never changes live data.

On normal shutdown, one ``DSP_GAIN_SHADOW`` JSON record is written to stderr.
It reports the fixed DSP authority, zero live native commits, admitted jobs,
unrecognized/LLE-only frames and per-route native/fallback calls, compared
and matching/differing words, matching/mismatching calls, diagnostic native
wall time and first differing sample/value. The public XISO host runner has
``--shadow``, ``--shadow-routes`` and ``--shadow-fault`` controls and validates
counter consistency independently. A shadow mismatch fails that campaign
while retaining the independent guest verdict, so a passing guest cannot
hide a broken candidate.

The job is submitted before the original frame and joined afterwards. Thread
scheduling determines overlap; this is neither a CPU-parallelism benchmark
nor an accelerated gameplay path. Candidate code uses only private words and
does not call APU, DMA, FIFO or guest-memory APIs. Program/state ineligibility
continues through ordinary DSP execution without a candidate submission.
