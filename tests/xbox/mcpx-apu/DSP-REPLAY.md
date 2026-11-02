# DSP replay work in progress

The JIT and C interpreter expose separate in-process checkpoints in
`dsp_capture.h`. Call them only while the caller owns the DSP execution
context. They copy DSP RAM and backend state without reading peripheral
registers; restoring invalidates the appropriate instruction cache. The JIT
checkpoint preserves the library's exposed 128-slot interrupt state. These
structures use native C layouts and the linked library's ABI: do not persist
or deserialize them directly.

`capture_wire.c` encodes the exposed JIT checkpoint fields into a fixed
41,554-byte little-endian version-1 payload (`XDJT`, 32-bit version), without
native padding or pointers. It records all 128 interrupt priorities and both
pending masks, registers, both stack banks, X/Y/P RAM, DMA state and DSP
interrupt flags. Decode rejects incorrect lengths, magic, version, booleans,
and unsupported power/interrupt states before changing the destination. This
format is JIT-specific; the library ABI and hash must still be recorded and
checked independently. It is a state payload, not a complete capture file or
an executable program sandbox. The test suite round-trips a checkpoint and
executes the decoded state through the real DMA path.

Version 1 layout (all offsets in bytes; integers little-endian):

| Offset | Contents |
| ---: | --- |
| 0 | `XDJT` magic, followed by 32-bit version 1 |
| 8 | 32-bit PC, PC advance, cycle count and signed cycle budget |
| 24 | One byte each: PC-on-REP, loop-REP, halted, power state |
| 28 | Two 64-bit pending-interrupt masks |
| 44 | Pipeline byte, 32-bit vector address, 32-bit saved PC, state byte |
| 54 | 128 signed 8-bit interrupt priorities, then one IPL-to-raise byte |
| 183 | 64 32-bit registers, then two banks of 16 32-bit stack words |
| 567 | 4,096 X, 2,048 Y and 4,096 P 32-bit words |
| 41527 | Six 32-bit DMA configuration/control/start/next/read-count/interrupt values |
| 41551 | One byte each: DMA error, end-of-list, GP identity |

Boolean fields permit only 0 and 1. Power and interrupt state values are
restricted to the values exposed by the current DSP 0.1.3 API; additional
semantics need an explicit format/adapter change. A valid state payload alone
is not proof that execution is safe or that the reference is a hardware
golden. The eventual replay process must impose execution and resource limits.

The checkpoint does not contain APU registers, frame division, external guest
RAM, mappings, FIFO cursors, host output or a complete peripheral transcript.
A caller must capture and restore those separately at a coherent boundary.
The opt-in recorder and replay paths below capture those components only for
their declared bounded models; they are not yet a complete authoritative
snapshot of every external input or guest-memory byte.

An experimental frame-boundary hook can be compiled with the Meson option
`-Dxemu_dsp_capture=true`; the option defaults to false and the default APU
frame path contains no hook. An owner holding the APU lock may attach a
preallocated, single-producer/single-consumer ring with
`mcpx_apu_capture_set_ring_locked()`. While attached, the APU records GP/EP
JIT state, APU registers and monitor PCM before and after the DSP frame,
mixbins, controls, phase and ordered scratch/FIFO callback data. Events use
the attached session's frame number times 32 as a boundary marker, not an
intra-frame timestamp. A full ring increments a drop counter and executes the
DSP frame once without recording; a full event/payload buffer marks its frame
truncated, and the next recorded frame identifies dropped predecessors. A frame
published without both boundary snapshots is marked incomplete. A non-JIT
backend is marked unsupported. Snapshotting and event
recording perform no allocation, serialization or disk I/O on the APU thread.
Reset, VM pause/resume, DSP preference changes and GP/EP MMIO writes mark a
discontinuity before the next recorded frame. Different P-memory contents at
the two boundaries are flagged as well; same-value writes or changes reverted
within a frame are not detected by that comparison. APU register words are
read atomically because guest MMIO can update them without the APU lock, but
the collection of words is not a coherent snapshot.

Every record retains `DSP_CAPTURE_EXTERNAL_UNVERIFIED`: the hook records
observed scatter/gather descriptor reads and physical transfers, but cannot
exclude concurrent guest RAM writes or prove that the mapping stayed fixed.
It inspects the descriptor mapping before and after the actual read, without
issuing another read, and flags observed MMIO, nonidentity aliases or partial mappings.
A mapping that changes and changes back between those observations can still
escape detection. With the
experimental option enabled, set `XEMU_DSP_CAPTURE_PATH` to a new private
file, `XEMU_DSP_CAPTURE_FRAMES` to 1–1,024 (default 32), and optionally
`XEMU_DSP_CAPTURE_SKIP_FRAMES` to 0–1,000,000 (default 0). The first requested
frames after that delay are handed to a separate writer; it creates the file
exclusively with owner-only permissions, never overwrites an existing file,
and finishes after the bounded session while the APU continues. This is not a
complete capture. The ring owner must detach under the APU lock and stop both
producer and consumer before freeing it. Unit tests exercise
the handoff, overflow, FIFO data, full-ring frame execution and unsupported
backend. On 2026-11-02 the two ring tests also passed on the Steam Deck using
a stripped Linux build-host binary and its bundled shared libraries; its
SHA-256 was `110eb396a5b7fbb07b2efdaff986a220d3be698922b9fecbf4b0e5ed8ab141ea`.
The standalone ring tests also passed locally under ASan/UBSan and TSan; these
sanitizers did not cover the full emulator or the Rust DSP library.
An isolated production GP/FIFO test compares capture off with two captures
from the same restored state using a side-effecting MMIO descriptor. It
checks that each run performs exactly two descriptor reads and that DSP
checkpoints, RAM, APU registers, monitor PCM and the two normalized capture
transcripts agree. A second test covers a descriptor that removes its MMIO
mapping while being read; the capture is marked non-RAM. These synthetic
tests do not establish equivalence for all devices or concurrent CPU activity.
These tests do not establish that enabling capture has no game-level timing or
audio effect.

The experimental XCR1 version 3 stream uses fixed little-endian fields and
per-record SHA-256 digests. Its checksummed 304-byte header records the capture
time in Unix microseconds, xemu commit/version, and the linked DSP library's
build-recorded version and archive SHA-256 (or an explicit `UNVERIFIED` status
for a system library). Each frame contains four XDJT checkpoints, before/after
APU-register arrays, mixbins, monitor buffers and an XCT1 logical and physical
transfer transcript. Older version 1 and 2 streams and XIO1 logical-only
transcripts can still be inspected, but they do not acquire missing build
identity retroactively.
An END1 footer contains written, dropped and lost counts. Use
`python3 tests/xbox/mcpx-apu/dsp_capture_stream.py <capture.xcr>` to check
the bounded structure and hashes. A well-formed stream still returns
`INCOMPLETE` and a nonzero exit status: it lacks independently verified
external memory and provenance. Missing footers, corrupt records and unknown
versions are not accepted as completed captures. The inspector can still read
version 1, but those older files did not record descriptor mapping checks.
Metadata corruption in version 3 is rejected before any frame is accepted.

On 2026-11-02, the capture-enabled Linux build was run on the Steam Deck with
an Xbox game image and private copies of writable Xbox files. After a 25,000
subframe delay it recorded eight adjacent frames and 433 scratch/FIFO events,
with zero reported drops and writer losses. The stream remains private and
has SHA-256
`b7c73babc95f106de41a8fc077f6486366813f63e51c228893e4d4656801151b`.
The stripped executable SHA-256 was
`7bd9d8334c153f2ff59f9f4c503087374ebfdf32b9579f0e77c78606a26d1c15`.
The final frame's EP P-memory differed across the frame, so treating these
frames as a fixed-program sequence would be incorrect. The precise game scene
and program provenance were not independently verified; this is an
**incomplete live capture**, not an isolated replay or hardware oracle.

A subsequent private capture with physical transfer tracing recorded eight
adjacent frames and 1,471 events with zero reported drops or writer losses.
Its SHA-256 is `9e1f74a1976c55d70a2a2b828bd8b53b0e840c02b8edc2796e76f0cf1a3ea365`;
the stripped emulator SHA-256 was
`8226800de7c7072f7b0c0cd2a5a2f054d093fa6d242379c6548e0adb12e2793b`.
The observed memory operations were consistent over 31,076 bytes, leaving
67,077,788 bytes unknown. An isolated Deck replay initialized unknown bytes
to zero and matched all eight monitor buffers, final DSP states, final modeled
RAM, and 431 logical transfers. The EP program changed during the final
frame. This is an unverified JIT baseline match under those assumptions, not a
hardware verdict or proof of a complete capture.
Reconstruction and replay were repeated on the Deck using the documented
tool; the stripped replay binary SHA-256 was
`b1017f2684f00050bc30d4d3410810ae37ac9b23c3314220e9e0b2488e6f8732`.
A later 100-run repeat on the Deck completed 100/100 baseline matches using
stripped replay binary SHA-256
`b917b63083e42e21a5f597100e3cfb412303fc4596abc41864d122ffed41ad43`.
This demonstrates repeatability of the reconstructed input, not completeness.
With per-subframe GP and EP checkpoints enabled, one further Deck replay
matched both DSP states and the monitor buffer at every captured boundary;
its stripped worker SHA-256 was
`8a4b39deec6ec1fe101f744a46bfb8ec8113bc98aff7fa0eaebac5a2e6c122b7`.
These experimental binaries were built from an uncommitted working tree
based on `d1fed222e531f8cb544925595876aa2fc62810c9`; the executable hashes
identify what was actually run, rather than claiming a clean commit build.

A fresh version 2 Deck capture, with an isolated copy of writable Xbox files,
contained eight frames and 1,480 events, with no reported queue drops or
writer losses and no descriptor observed outside the expected RAM mapping.
Its SHA-256 was
`383f5e9dea26ccada8a0b77b4537500d65866247da98e85552b3f9173e8e9703`;
the stripped emulator SHA-256 was
`9ce65eb134c310dfb83829adea634f7c8ca2c930323585cec7cc233a9fa1de7d`.
The observations covered 28,516 bytes and left 67,080,348 bytes unknown.
One hundred isolated replays on the Deck matched every captured GP/EP frame boundary,
monitor buffer, final modeled RAM and 434 logical transfers using stripped
worker SHA-256
`82797f2f4d0f788ef9846744c18bff33e2359ae5525a408dc872f9351791d144`.
The run used dummy host audio; it is not an audio-device or timing test.
The sequence, provenance and guest-memory coherence remain unverified.

A fresh version 3 private Deck capture subsequently recorded eight contiguous
game frames and 1,477 events with zero queue drops or writer losses. Its
SHA-256 was
`0339708d3de5db6394dd4b60fd8441f1ede71e3854dfad47dd994910e8843c03`
and the stripped emulator SHA-256 was
`10b031a4cffb8bc15b5024102b220430141f44e1e9ba71dad6afd0a679abcc7c`.
The embedded header recorded base commit
`d1fed222e531f8cb544925595876aa2fc62810c9`, xemu version
`0.8.136-0-gd1fed222e531`, and linked DSP v0.1.3 archive SHA-256
`d1dca04950626872e8d0cc9376ceaca8e3fdd586fdd78da622d8dcb5ec956d67`.
Physical observations covered 28,528 bytes and left 67,080,336 bytes unknown;
the reconstruction retained 431 logical events. One hundred isolated Deck
replays matched every captured GP/EP frame boundary, monitor buffer, final
modeled RAM and ordered logical transfer. The stripped replay worker SHA-256
was `104fc244b12fd42c8262899c0540fc5f8a907f3fbb8e8fad8cef4d9bad1ee3b2`.
The run used dummy host audio and did not retain a public title/scene identity.
It is repeatable private emulator-baseline evidence, not a hardware oracle,
complete guest-RAM snapshot, audio-device test or redistributable corpus case.

`dsp_capture_reconstruct.py <capture.xcr> --ram-size 67108864 --output <new-dir>`
validates transfer coverage and adjacent boundaries, and writes private replay
inputs with unknown RAM initialized to zero. It also writes a known-byte bitmap
and hashes, plus `gp-frame-states.bin` and `ep-frame-states.bin` containing
the ordered post-frame XDJT states. Pass both files to `verify-mapped-trace`
with `--gp-frame-states` and `--ep-frame-states` to compare GP, EP, and PCM
at each boundary and report the first failing subframe. I/O is checked as it
occurs; ordering within one subframe is not a full event chronology. Without
the optional pair, only final DSP states are compared. Its successful artifact
creation still reports `INCOMPLETE` and
returns a nonzero status; provenance and external memory remain unverified.
Version 2 or 3 frames missing the mapping-observation marker, and frames with a
detected non-RAM descriptor, cannot be reconstructed as plain RAM. Version 1
requires the explicit `--allow-legacy-unverified` option and is labeled
`NOT_RECORDED`. Existing private captures retain their recorded stream version;
only captures made by the current writer contain the version 3 provenance
header.

`dsp_corpus.py` is an early payload-integrity check. `INTEGRITY_PASS` means
only that the listed preliminary metadata and payload hashes pass its checks;
it does not mean a case is complete, replayable, sealed, or correct. Its
format and validation are not yet the full issue #383 corpus contract. Do not
promote its output to a golden or use it as a correctness verdict.

Upstream ISA baseline (2026-11-02): an independent checkout of
[`xemu-project/dsp56300` at `b36db2c23ee2dd6e643f48e4dc6288074030e1a0`](https://github.com/xemu-project/dsp56300/tree/b36db2c23ee2dd6e643f48e4dc6288074030e1a0)
was built with its pinned Rust 1.99.1 using `cargo build --locked --release
-p dsp56300-emu --bin difftest`. Its `check_goldens.py --difftest
<release-difftest> --mode step` and the corresponding `--mode block` run
each matched 562/562 cases in 24 sealed hardware-derived banks, with zero
reported differences. The release binary SHA-256 was
`e62ded50f5ebbbd16eb0a54431d4ca467412e4fe874b44f80bc31699c7d2b4a7`.
The first build attempt with system Rust 1.85 failed on the upstream
dependencies' compiler requirements; no test result was produced by that
attempt. These tests cover the upstream v1.0.0 implementation, not xemu's
linked v0.1.3 fallback or MCPX peripheral and timing behavior. Upstream's
`docs/LIMITATIONS.md` describes excluded and deferred semantics.

The CI workflow includes separate upstream-golden and xemu replay jobs.
`dsp_upstream_goldens.py` requires the pinned clean checkout and all 24 sealed
banks, runs both modes, and writes a non-overwriting JSON report with the
harness hash. The xemu job separately records the replay worker's linked-DSP
identity on a synthetic halted-core case. These jobs are defined on this
working tree; they have not yet run on GitHub Actions.

`dsp-boundary-fuzz.py run --worker <worker> --seed 47 --count 24 --output
<new-directory>` exercises synthetic GP-to-EP scratch DMA and GP FIFO wrap
across two scatter/gather pages against the mapped replay worker. It varies
one dimension at a time and then combines data values, physical pages and
1/2/8/9/128/1024-subframe sequences. The
report includes the number of distinct input tuples, the program and worker
hashes, per-dimension coverage and the fixed seed. Expected side effects are
derived from the existing synthetic emulator-baseline fixture; this is not
independent hardware evidence. On the first nonpass, the tool retains the
original case and greedily tries shorter sequences, lower pages and simpler
values while preserving the same mismatch location. A reduced case is saved
only when a simplification succeeds; it is not guaranteed globally minimal.
Use `--start` to run a reproducible slice of the seed's case sequence.
`dsp-boundary-fuzz.py replay --worker <worker> --case
<case-directory>` verifies payload hashes and replays that case.
`--inject-fault` deliberately corrupts expected RAM for a runner self-test;
its results must never be treated as a normal campaign or a golden.
SIGINT or SIGTERM requests a bounded stop after the current worker and writes
`CANCELLED` with attempted and completed counts; it is never a passing run.

On 2026-11-02, the combined seed-47 campaign completed 24/24 cases on the
Steam Deck, including 22 distinct input tuples. Its input sequence SHA-256 was
`225bb61fde491cbf50072dc2b0f6fc49715c2bb2fa304ab871a628f83db34dc4` and the
stripped worker binary SHA-256 was
`9682c2224c85dbabd859fc1d06910254c5fbfe622cbe319b964d9489c39ff101`.
Both DMA modes included each of the six frame counts.
The five boundary-runner process tests also passed on the Deck, including
fault injection, reduction, replay, corruption rejection and cancellation.
The worker was built on Linux and run on the Deck with bundled libraries.
An earlier scratch-only seed-47 campaign completed 1,000/1,000 cases locally,
with 316 distinct inputs and input sequence SHA-256
`3d43a89f3595a6f8fa2cd12ac5ac1ee95e9140fcf38ac1735e1ac70d1ae4221b`.
The combined seed-2026 campaign completed 1,000/1,000 cases locally, with
324 distinct inputs and input sequence SHA-256
`3927982485cce1d8eed8f6c6348300a24308b3bdf17eccad7937e3d8da214b03`.
Its worker SHA-256 was
`008004989f6da0641cf082197f0fb1011ed03204b3b48c81d8a6561cac8fcb2b`.
These two synthetic workloads and JIT execution do not cover the required
broader MCPX mutation categories, alternate engines, or hardware behavior.

`dsp-engine-fuzz.py run --worker <worker> --seed 89 --count 24 --output
<new-directory>` runs seeded JIT-versus-interpreter comparisons for the three
fixed `verify-engines` fixtures, including EP PCM output. The schedule varies
the 24-bit value, sequence length and one of ten fixed GP input signals
separately, together, and as controls. The signals are impulse, silence,
alternating half-scale, ramp, left-only, right-only, alternating full-scale
and alternating one-LSB samples, deterministic integer noise, and an
integer-table swept sine. EP-sink cases retain the impulse selection
because that fixture disables the GP and its output is independent of the GP
mixbins. The tool does not mutate arbitrary DSP programs. Reports record
per-result counts, completed work, fixture/dimension/frame/signal coverage,
distinct input tuples, and the worker, wrapper, runner and linked-library
identities. On a mismatch it retains a fingerprinted case and greedily tries
shorter sequences, simpler values and the impulse signal while preserving the
first reported failure location. `dsp-engine-fuzz.py replay --worker <worker> --case
<case.json>` requires the recorded worker, wrapper and runner hashes. A
deliberate `--inject-fault` is a runner self-test, not qualification; a
cancelled campaign is not a pass.

An earlier version-1 signal schedule on 2026-11-02 matched 120/120 cases,
including 40 for each
fixture and 72 distinct input tuples; there were 12 cases with 1,024 subframes.
No mismatch, timeout, crash, unsupported or incomplete result was reported.
The input-sequence SHA-256 was
`bf391bda2a7de82a66b3f80f9128a959cbbe2d0aaa29986c144fa1f02f4911af`, the
worker SHA-256 was
`4da2f0f1094a07497fce699f26aade0edb5af1fc1aae87d8a13b9c143ed4854f`, and the
runner SHA-256 was
`dda64ad1c349bb4365b0eb41e13744770c90867ad2c7a3f695f022a55d130078`.
Those identities describe the earlier runner, not the current schedule.

The current version-2 seed-89 schedule subsequently matched 150/150 cases:
50 per fixture, 92 distinct input tuples, all ten signals, and 12 cases for
each of 1, 8, 128 and 1,024 subframes. No mismatch, timeout, crash,
unsupported, invalid, inconclusive or incomplete result was reported. Its
input-sequence SHA-256 was
`10ea0e4011c1b1f4b97f60223e6ed8ffa424a27268e0e70ebd325f64485cfc17`,
the worker SHA-256 was
`d81efe12e8ca88bb1dcd8f543740ce920fab71c9a9aba73d2115afdc59fd3623`,
the wrapper SHA-256 was
`6642d72e719d5cb881a6f5fc37542c2ad9f19b42079175b4e24a3a5d46de893c`,
and the runner SHA-256 was
`71bcffa5512aba515066e9ff0350bfe74904366deee3ccd8360a60b7f28cdb42`.
Both the capture-enabled and default-DSP builds passed all 11 focused DSP test
targets after this run. An earlier Steam Deck run passed the eight
boundary-runner tests and two direct eight-signal/invalid-selection tests using
stripped worker
SHA-256
`5cec132e9bb1df63520d1a4be5a36df1cc85ec80bcbccf12a0f8887ed8b81440`
with bundled build-host libraries. A subsequent Deck check matched the new
deterministic-noise scratch and swept-sine FIFO cases for nine subframes each,
using stripped worker SHA-256
`2058def96b3ad0a92b9f004ec3a73c529bc730e481d3d522c2041ce4c4e012b0`.
These are bounded synthetic emulator
comparisons, not hardware-golden, real-game, audio-device or HLE qualification
results. The 24-case CI job is configured but has not run on GitHub Actions.

`dsp-replay.py verify-engines --worker <worker> --fixture <scratch|fifo|ep-sink>
--frames <1..1024> --value <24-bit-integer> --signal <pattern>` compares the
production JIT and C interpreter on one fixed synthetic program with a selected
deterministic GP input. `--reference` and `--candidate`
also allow the same backend on both sides. Each side has separate APU/DSP
objects and RAM images; a single private QEMU address space is populated from
the relevant image before each side runs. At each subframe the tool compares
ordered scratch/FIFO transfers, RAM, APU and GP/EP registers, monitor PCM,
guest-readable DSP X/Y/P memory, DMA state, interrupt flags and halt state.
It also compares each DSP's PC, data registers X0/X1/Y0/Y1 and A/B
components, and R/N/M address-register banks using the backends'
non-destructive native checkpoints. Cross-engine comparisons widen legacy C
signed N offsets and its 16-bit linear M sentinel as in the existing JIT
snapshot import; JIT values are never truncated. Same-engine comparisons
compare raw values. SR, OMR, SP, SSH, SSL, LA, LC and both 16-entry stack
banks are also compared in full. The fixed programs do not exercise every
status bit or stack operation; the full interrupt pipeline is not yet
compared or normalized between engines. A separate production-engine probe
checks a JSR/RTS call and the resulting return-address stack entry.
When both sides use the same engine, each comparison also checks every field
exported in that backend's native checkpoint, including its interrupt
pipeline, pending slots, memory and saved cycle budget. The report counts
these checks separately; cross-engine comparisons report zero. The native
checkpoints do not establish that every internal backend field is exported,
and the fixtures do not exercise every interrupt transition.

A separate headless RTS-at-empty-stack probe starts with no pending interrupt
and compares two independently initialized checkpoints after each of ten
instructions. Each backend observes a pending or active interrupt; at step 3,
the test changes one copy's memory, restores the active-interrupt checkpoint,
and checks subsequent execution. The interpreter test deliberately disables
`exception_debugging` in its private checkpoint, which now captures and
restores that setting. With the default setting and assertions enabled, the
same interpreter path aborts; a child-process test verifies that outcome. On
the Deck, the probe passed 10/10 runs for each backend with the test setting
and reproduced the default interpreter assertion. The stripped executable's
SHA-256 was `6c12c8cacb296405ded2f85f2da7c8ec267ec8c6a52dbda4075aebaced310abc`.
These same-engine checks are not cross-engine or silicon equivalence.
The fixed fixtures also assert their expected transfer count/data, full RAM and
every active GP-block PCM sample (or every EP-sink sample) so that identical
failures do not automatically pass. The full-scale and one-LSB patterns cover
conversion boundaries without claiming that these fixed programs model signal
processing effects. Selecting a GP signal for the GP-disabled EP-sink fixture
is rejected rather than reported as coverage.
I/O sample indices mark subframe boundaries; they are not intra-frame timing.
Mismatch reports include both engines' GP/EP program counters at the end of
the failing subframe, not the exact instruction that caused the divergence.

The comparator initializes both backends through their own reset and memory
interfaces. For these fixtures it sets SR=0 and OMR=2 on JIT instances,
matching the interpreter's observed reset values; their unmodified JIT reset
values were SR=0xc00300 and OMR=0x300. The fixtures therefore do not compare
the unmodified reset modes. Earlier comparisons before these fields were
checked used unequal initial modes and do not establish same-state equivalence.
The comparator checks its observable state subset before execution. An initial
difference is reported as `UNSUPPORTED`, not an execution divergence. This
does not translate arbitrary JIT checkpoints into C snapshots or prove all
hidden state equivalent. It records emulator agreement as
`BASELINE_MATCH` with `UNVERIFIED` authority, not a hardware result. Its
`--inject-*` options deliberately corrupt inputs or observations to test the
comparator and must not be used as normal qualification. Peripheral/IRQ event
ordering within a subframe and concurrent guest CPU activity remain uncovered.

On 2026-11-02, local JIT-versus-interpreter checks matched all 75 combinations
of the three fixtures, 1/8/9/128/1024 subframes and values
`0`, `1`, `0x7fffff`, `0x800000`, `0xffffff`. The worker SHA-256 was
`bfb2e3f9f1ff89c6011fa01c5b2e489a643dfcb402e7ebe97cb1a2ed32e74680`.
On the Deck, nine-subframe runs matched 100/100 repetitions for JIT versus
JIT and interpreter versus interpreter on scratch, and for JIT versus
interpreter on each of scratch, FIFO and EP sink. The stripped Deck worker
SHA-256 was `533b3f62eac675727ef4e01bbce66d4d27056abc9e31518686c5e4268d3b5547`;
it used bundled build-host libraries. These checks are limited synthetic
emulator comparisons, not a qualification of real-game captures or silicon.

The production-linked integration test executes synthetic GP and EP DSP
programs through xemu's actual peripheral and DMA handlers, checks their
scratch writes, tests the GP/EP frame entry and EP cadence, and restores the
checkpoints repeatedly. The interpreter test covers checkpoint restore and a
DMA status read with side effects. Additional isolated tests exercise the
production GP FIFO/SGE wrap and EP audio sink. These are emulator baseline
tests; none establishes hardware correctness.

`dsp-replay-io.c` provides a bounded in-memory event transcript for scratch and
FIFO transfers. It checks request kind, index, address, direction, caller-
supplied sample index and length in order; writes must match the recorded
bytes. Exhaustion and mismatch prevent completion. The DMA fixture records
the actual production DSP/DMA callback sequence and replays it against a
separate memory model, checking the model's read bytes against the transcript.
This fixture uses a synthetic sample index of zero: production sample timing,
MMIO events, persistence and general device modeling are not yet implemented.

`dsp-replay-core.c` provides a single-threaded JIT execution primitive for a
restored checkpoint, isolated bounded scratch memory and an ordered transcript.
It steps the production backend up to a caller-specified instruction limit;
it publishes resulting state and memory only after halt and full transcript
consumption. Read/write mismatches, unmapped scratch, FIFO access and timeout
are reported separately. `HALTED` is not a correctness verdict: a caller must
compare the resulting state and memory to a qualified reference. A single DSP
step can enter an unbounded production DMA operation, so an external process
timeout remains mandatory for untrusted inputs. This primitive does not yet
run full GP/EP frames, capture MMIO, compare PCM or load corpus files.

The experimental standalone `verify-core` command compares two JIT state
payloads in a fresh, timed worker with no external I/O. `verify-scratch` also
loads a bounded scratch-memory image and ordered I/O transcript, then compares
the resulting state and memory. Both report
`BASELINE_MATCH`, `MISMATCH`, `TIMEOUT`, `INCOMPLETE`, `UNSUPPORTED`, `INVALID`,
`INCONCLUSIVE`, `NONDETERMINISTIC` or `CRASH`; a baseline match is not a hardware or APU oracle,
and the reference authority remains `UNVERIFIED` until manifest admission.
If a repeated run first matches and then reports a mismatch with the same
recorded input and worker hashes, the wrapper reports `NONDETERMINISTIC` and
retains the mismatch details; unrecorded environmental influences remain
possible because the worker is not a security sandbox.
The report hashes both state files and the worker before and after each run.
When Meson selects the xemu fallback dependency, its archive is an explicit
input to a generated worker header; the report includes that build-time
archive SHA-256 and fallback version as `BUILD_RECORDED`. This is build
provenance, not an independent runtime attestation. System-library builds and
the DSP API revision remain `UNVERIFIED`. There is no capture manifest
admission yet. On POSIX hosts the wrapper limits wall time, guest instruction
count, address space, CPU time and worker output-file size, and disables core
dumps. The report records the effective limits; options include `--memory-mib`
and `--cpu-seconds`. Platforms without those POSIX limits return `UNSUPPORTED`.
This is not a security sandbox: it does not isolate filesystem or network
access. Use trusted synthetic inputs until complete I/O modeling and platform
qualification are implemented.

```sh
ninja -C build-capture dsp-replay-worker
python3 tests/xbox/mcpx-apu/dsp-replay.py verify-core \
  --worker build-capture/dsp-replay-worker \
  --initial initial-state.bin --expected expected-state.bin \
  --steps 1000 --repeat 100 --timeout 10
```

For scratch I/O use `verify-scratch` with the same options and add `--memory`,
`--expected-memory` and `--events`. Memory is a flat byte array of at most
16 MiB; the expected image must have the same length. `--events` is an `XIO1`
file: four magic bytes, a 32-bit little-endian event count, and then records
consisting of one-byte kind (0 scratch, 1 FIFO), one-byte direction (0 read,
1 write), two zero reserved bytes, 32-bit index, 32-bit address, 64-bit sample
index, 32-bit payload length, and the payload bytes. Integers are little-endian;
at most 4,096 events and 4 MiB of payload are accepted. The current execution
adapter supports scratch only and uses sample index zero. Missing recorded
requests are `INCOMPLETE`; a changed request, response or final memory is
`MISMATCH`. FIFO, timed events and general guest mappings remain unsupported.

`dsp-replay-frame.c` is a GP/EP frame adapter. It restores both JIT
checkpoints, preserves a supplied 256-sample
monitor buffer, and calls the production `mcpx_apu_dsp_frame()` for one
32-sample GP subframe. EP executes at phase zero of eight. GP and EP use
separate scratch images and share an ordered event log; scratch event index 0
identifies GP and index 1 identifies EP. It currently enables both DSPs and
selects GP monitoring, with one 1,000-cycle production run per active DSP.
It does not reproduce APU registers, SGE page maps, EP PCM output, FIFO,
automatic multi-frame timing or capture concurrent guest memory. A synthetic
test chains nine calls, carrying both DSP checkpoints and the monitor buffer
through a full EP cadence and into the next EP frame. Failed execution does not
publish its state, PCM or scratch outputs. As with the core adapter, an external
process watchdog is required.

`dsp_replay_gp_ep_sequence()` chains 1–1,024 such subframes, with a separate
bounded transcript for each. It carries forward both DSP checkpoints, each
scratch image, the monitor buffer and EP phase, and returns each subframe's
32 stereo GP-monitor samples. A failed frame identifies its index and leaves
the caller's state, memory and PCM outputs untouched; transcripts already
examined may be consumed. This adapter still lacks actual subframe event timing,
real SGE mappings, FIFO and EP output. Production-linked synthetic tests cover
1, 8, 9, 128 and 1,024 frames and a failure on frame 3.

The timed `verify-frame` command exposes this narrow adapter. It requires
`--gp-initial`, `--ep-initial`, `--gp-expected`, `--ep-expected`, `--mixbins`,
`--prior-pcm`, `--expected-pcm`, `--gp-memory`, `--ep-memory`,
`--gp-expected-memory`, `--ep-expected-memory`, `--events` and `--phase`.
Mixbins are 1,024 little-endian IEEE binary32 values in mixbin-major order;
the two PCM files are each 256 interleaved stereo signed 16-bit little-endian
samples. Phase is 0–7. The result compares both backend states, every PCM
sample and each scratch image, and identifies the first mismatch. It remains
limited to GP monitoring and the synthetic scratch model above; a match is
not an EP-output, hardware or complete-game correctness verdict.
PCM mismatch reports include the zero-based sample and channel in the supplied
256-sample monitor buffer, as well as the flattened interleaved index.

`verify-sequence` uses the same file options and phase, plus `--frames` from
1 to 1,024. Its mixbins file contains one 4,096-byte mixbin-major block per
subframe. Its expected PCM contains the 32 interleaved stereo signed 16-bit
samples per subframe in execution order. The events file is `XIS1`, a 32-bit
little-endian frame count, and then for each frame a 32-bit byte length and
one complete `XIO1` transcript. The entire sequence accepts at most 4,096
events and approximately 4 MiB of event payload. PCM mismatch sample indices
are relative to the beginning of the sequence. A matching sequence remains
an unverified JIT baseline, not a hardware or full APU verdict.
Process-level synthetic checks cover a two-frame EP boundary and a 1,024-frame
sequence; the latter checks all output samples and the final GP and EP states.

`verify-mapped-frame` runs one frame in a fresh worker using the production
GP/EP scratch, FIFO, SGE and monitor callbacks against one private guest-RAM
image. It uses the same four GP/EP state files, mixbins, prior PCM and expected
PCM options as `verify-frame`; instead of separate scratch images and events,
it requires `--apu-registers`, `--expected-apu-registers`, `--ram`,
`--expected-ram` and `--control`. The register files contain 0x20000
little-endian 32-bit values in the same array order as `MCPXAPUState.regs`.
RAM files must have equal, nonzero, target-page-aligned lengths up to 128 MiB;
they contain a flat physical address space starting at address zero. The
16-byte control file contains `XMF1`, 32-bit little-endian GP and EP reset
registers, followed by bytes for monitor point (0 AC97, 1 VP, 2 GP, 3 EP,
4 GP-or-EP), EP phase (0–7), GP realtime and EP realtime (each 0 or 1).
The worker compares both final DSP checkpoints, the 256-sample monitor buffer,
every APU register and every RAM byte. Initial control and all files are hashed
in the report. The wrapper's external timeout is essential because a realtime
DSP or malformed production DMA can hang in a single frame; malformed
peripheral state can also trigger a production assertion and report `CRASH`.

The process-level tests exercise GP FIFO wrap across two noncontiguous SGE
pages, EP FIFO PCM sinking and resulting guest-RAM silence, and a 64 MiB
synthetic RAM image. This mode has no ordered bus/MMIO/interrupt transcript,
does not reproduce MMIO regions outside that flat RAM image, and cannot
establish coherent live capture or independently verify timing. It uses the
JIT baseline. Neither the RAM image nor an end-state match proves that a
capture contains every interaction.

`verify-mapped-sequence` uses the same files as `verify-mapped-frame` plus
`--frames` (1–1,024). The mixbin file is a concatenation of one 4,096-byte
input per GP subframe, and the expected PCM file contains the entire
1,024-byte monitor buffer after each subframe. These buffer snapshots can
overlap in guest time; they are not 256 newly produced samples per subframe.
The initial RAM and APU registers are loaded once and retained between frames,
including FIFO cursors and GP/EP aliases to the same physical memory. EP phase
advances modulo eight as in the frame loop. A PCM mismatch identifies the
subframe and the sample/channel within its buffer; an execution timeout or
invalid input can identify the failing subframe. A hard process timeout or
crash may not identify one. Tests cover 1, 8, 128 and 1,024 subframes, the EP
sink, and a GP scratch write read by EP in the same frame. They use synthetic
inputs and do not record external CPU writes between subframes.

`verify-mapped-trace` takes the same inputs as `verify-mapped-sequence`, plus
an `--events` XIO1 transcript. In the private worker, it wraps the production
scratch and FIFO callbacks, checks their order, unit, direction, address,
length and data, and still calls the original callbacks. Scratch event index
0 is GP and 1 is EP; FIFO index is `(unit << 16) | fifo_number`. Each event's
sample field is the zero-based subframe number times 32; it identifies the
boundary, not the exact point within that subframe. Both read and write bytes
are compared against observed data from the independent RAM model. Recorded
read bytes are never injected into the production callbacks. Unexpected,
changed or missing transfers fail; a truncated transcript is `INCOMPLETE`.
The GP-write/EP-read fixture tests transfer order, altered read bytes, and an
unrecorded request. The in-process recorder test checks bounded-buffer overflow
and rejects the result without publishing partial outputs.

The replay comparator covers DSP scratch and FIFO callback boundaries. The
experimental live recorder additionally observes descriptor reads and physical
SGE transfers, but does not establish coherent concurrent CPU writes, capture
peripheral MMIO or IRQs, or provide intra-frame timestamps.

On 2026-11-02 the 21 process-level CLI tests passed on the Steam Deck (x86-64,
Linux `6.16.12-valve24.5-1-neptune-616-gb2f7cfe85e45`). The test used a
stripped copy of the Linux development worker with its build-host shared
libraries, rather than a native Deck build. The stripped worker SHA-256 was
`8ac2b937959fc70c034de70321306c1bc7d7043067afd7a3d29e3601d6772c0a`;
the linked fallback DSP archive SHA-256 was
`d1dca04950626872e8d0cc9376ceaca8e3fdd586fdd78da622d8dcb5ec956d67`.
The initial Deck attempt omitted the identity-generator script and failed one
test; after adding that script that earlier 20-test run passed. The later
21-test run includes mapped sequences and ordered callback checks. This was synthetic replay only;
no game, native audio output, performance or hardware correctness was tested.

On the Linux development build based on xemu commit
`d1fed222e531f8cb544925595876aa2fc62810c9`, a synthetic two-word halt
instruction was replayed 100 times in separate worker processes; all 100
matched its expected JIT checkpoint. This single no-I/O case is emulator
baseline repeatability evidence, not a complete corpus, a hardware result,
or qualification of a game capture.

Run the current focused tests after configuring an Xbox build:

```sh
meson test -C build-capture test-xbox-dsp-jit-capture \
  test-xbox-dsp-replay-dma test-dsp-replay-cli test-dsp-corpus --print-errorlogs
```

Remaining work includes the complete public corpus/sealing contract, complete
external-memory or device-model authority for game captures, additional
state/control mutation categories, the required 10,000-case CI and 100,000-case
nightly campaigns, Windows/sanitizer qualification, and redistributable public
GP/EP cases. Hardware authority applies only where an exact case is supported
by independent hardware evidence.
