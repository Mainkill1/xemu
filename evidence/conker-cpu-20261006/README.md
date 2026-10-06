# Conker CPU attribution after depth/upload repairs

Diagnostic integration source `65db50718535cb7821f68b47769e2d3bd434ea40` combines current main (Auto-four audio), #290 and #291. It is not an isolated candidate-vs-parent measurement. Exact release build ID `31cdaa73695686b6cb032996ee31f8bb997ee4c3` matches both the recorded executable and its release DDEB; perf resolved symbols with the matching ELF/debug file. Raw unsymbolized offsets are file offsets and must not be passed directly to addr2line as virtual addresses.

Run `20261006-111147904-88e50aa1a7594f99a40d6ec2d957e559`, Steam Deck AMD Custom APU 0405. Both retained start/end images show the Conker Xbox Live & Co menu. The frozen procedure, controller, delays, configuration and measurement boundary are unchanged. This separate diagnostic explicitly allows operator profiling. perf recorded user-mode cycles at 99 Hz for 25 seconds after the authored recording-start checkpoint; about 5,000 samples, zero lost. No call stacks were captured. Raw perf data and commercial-game images remain private.

All-process self-cycle shares: xemu 55.64%, JIT 18.71%, libsamplerate 5.18%, Vulkan RADV 3.65%. These module shares are not complete subsystem attribution: APU work also lives in xemu, and renderer work also lives in drivers.

The named TB lookup/cache/tree functions sum to 9.28% of sampled cycles. Shared `qht_lookup_custom` adds 3.47%, but cannot be assigned exclusively to TB lookup without caller stacks. The combined 12.75% is therefore a candidate investigation scope, not an exclusive cost or predicted speedup. No hit/miss distribution or guest target identities were recorded. Existing #311/#166 own follow-up; #301 has an unresolved above-capacity regression and is not automatically justified by this profile.

The profile narrows the next CPU investigation while native depth ownership, balanced performance and XISO qualification remain incomplete. It does not recommend merging #290/#291 or claim a sustained 30 FPS result.
