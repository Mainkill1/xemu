# Dispatch assembly and footprint check

This is read-only inspection of the already measured standalone executables and the exact native parent/candidate executables. No product source, build, scenario or campaign input was changed. Binary/debug identities and `nm --print-size` lines are in [identity.json](identity.json); compressed `objdump` outputs retain the full instruction range.

## Function code size

| Build | Parent bytes | Candidate bytes | Candidate minus parent |
|---|---:|---:|---:|
| Standalone GCC14.2/LTO | 2173 | 2077 | -96 |
| Standalone Clang19.1.7/ThinLTO | 2060 | 1780 | -280 |
| Native CI Clang21.1.8 release | 2077 | 1787 | -290 |

These are ELF symbol extents for `dsp56k_execute_instruction`, not total executable size, dynamic instruction counts, instruction-cache measurements or game savings. GCC additionally has a cold fragment: 124 parent bytes versus 93 candidate bytes. The 4,096-entry data cache stays 32 KiB/core on these 64-bit hosts. Both native executable files have the same padded total length of 25,548,144 bytes; equal file length does not mean equal code.

## What the assembly establishes

The candidate's warmed dispatch loads a handler pointer and checks it, without the parent's normal/parallel classification and second normal-handler pointer load. The indirect call remains. Native candidate offsets `+0x71..+0xc5` include the cache load, miss-only selection and store; the common indirect call is at `+0xc8`. Native parent region boundaries are retained in the profile evidence.

The GCC post-call layout also changes outside the selected region: parent `+0x97` jumps on an active REP state, leaving the inactive path to fall through; candidate `+0x7c` jumps when REP is inactive. Candidate layout places REP handling between the call and the ordinary PC update. This gives a concrete layout difference to investigate if the approximately 9.6–9.7% GCC normal-only regression matters. It does not prove that one branch caused the measured loss. No branch/cycle PMU attribution was available, and shared-host frequency/activity were uncontrolled.

A cold parallel instruction previously selected its table handler without writing a per-PC cache entry. The candidate's first use writes that entry, which removes future classification but adds cold work. The Clang cold-table lane is approximately 3% slower, with clearing included in the batch. This is consistent with an amortization tradeoff; it is not an isolated first-decode cost measurement or causal proof.

The native dump uses exact function address/size from matching debug symbols. Because the executable is stripped, objdump's nearest exported C++ symbol labels are unrelated to the actual DSP function; use the recorded ELF range, not those fallback labels. The initial debug-file discovery found two parent paths for the same build ID and stopped; the final inspection used the canonical `usr/lib/debug/.build-id` path explicitly. No alternative executable was selected.

## Decision

Smaller code and a flatter hot path do not cancel the published regressions. Keep the measured candidate fixed through native AAAA/ABBA/BAAB. A layout or branch-hint follow-up would be a new candidate with its own evidence; none was inserted into the running comparison.
