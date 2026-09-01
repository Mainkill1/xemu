# ENG511 Vulkan/OpenGL shared hotpaths

Same `eng504` executable, 60-second foreground captures, fixed snapshots, guest
flip FPS:

| Scene | Vulkan | OpenGL | OpenGL change |
| --- | ---: | ---: | ---: |
| Morrowind | 22.97103 FPS | 42.34442 FPS | +84.34% |
| PGR2 | 26.44401 FPS | 25.13740 FPS | -4.94% |

Morrowind exposes a large Vulkan-specific deficit. PGR2 prevents treating that
as a universal backend ranking.

## Shared ETW modules

Ranked by the minimum percentage across all four traces:

| Rank | Module/path | Shared score |
| ---: | --- | ---: |
| 1 | `xemu.exe` | 36.884% |
| 2 | `ntdll.dll` | 14.964% |
| 3 | anonymous TCG/JIT | 13.576% |
| 4 | `ntoskrnl.exe` | 11.476% |
| 5 | `nvoglv64.dll` | 2.242% |
| 6 | `nvlddmkm.sys` | 0.681% |
| 7 | `msvcrt.dll` | 0.450% |
| 8 | `dxgkrnl.sys` | 0.191% |
| 9 | `dxgmms2.sys` | 0.073% |
| 10 | `kernel32.dll` | 0.069% |

The GCC release has no PDB, so ETW cannot honestly split `xemu.exe` or generated
TCG code into named functions. Source-to-stack mapping gives these first code
targets:

1. bounded PFIFO-to-PGRAPH packet batching;
2. translated-block execution/chaining counters and hot successors;
3. bulk `INLINE_ARRAY` / `ARRAY_ELEMENT16/32` handlers;
4. measured SoftMMU/TLB and page-bounded fast-RAM paths;
5. unchanged subchannel context-copy skip;
6. zero-cost release lab/trace gates;
7. identical uniform-write dirty suppression;
8. common shader-state and uniform-delta caching;
9. page-bounded `REP MOVS/STOS` bulk path;
10. APU voice-word, SGE-run, and unity/mono resampler fast paths.

Natural packet/page boundaries are the initial limits. No unexplained fixed
packet, byte, or time cap is acceptable.

For the immediate Vulkan deficit, instrument Vulkan finish/wait reasons and
PFIFO/PGRAPH lock ownership first. Shared CPU optimization alone cannot explain
Morrowind producing 84% more frames through OpenGL.

