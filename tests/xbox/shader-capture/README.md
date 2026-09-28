# Native 32-use shader capture fixture

## Independent behavioral specification

This scene is authored for PR #259's renderer-adapter acceptance gate. It runs as an ordinary Xbox application on xemu or hardware. It should emit 32 separate triangle-list draws per guest frame with one shared pixel program, distinct six-vertex ranges, per-draw vertex constants and pixel constants, and alternating equal-format texture allocations. The display is an 8-by-4 grid. A bounded recurring change on one tile provides a visible animation defect to mark during rolling capture.

Tests must count this shader's actual captured occurrences rather than infer 32 engine objects. Save/reopen must preserve every occurrence's inputs. Unedited original-camera replay must be compared with observed checkpoints; edited GLSL must be compared across all 32 uses. Native rendering and those recording/replay checks are separate gates.

## API research boundary

The implementation uses nxdk/pbkit's public pushbuffer, allocation, shader-compiler, and frame-presentation contracts. Study reference: XboxDev/nxdk `29638d0b001f179b73c3513489af10ddc2986216`, MIT API/library, and the NVIDIA Cg compiler provided by that SDK under its own tool license. The public pbkit header and nxdk triangle/mesh behavior were studied; no sample code, shader, geometry, or texture asset is copied. Standard NV2A register names and bitfields necessarily match the SDK and xemu hardware definitions. This is one-author OSS-informed implementation, not a claim of personnel-separated clean-room certification.

- Public API: https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/pbkit.h
- Register cross-check: `hw/xbox/nv2a/nv2a_regs.h` in this repository.

Keep an upstream SDK checkout read-only. Use a writable SDK copy or the project's isolated test toolchain when building. Generated shader includes, binaries, and disc images are build artifacts.


## Filled topology coverage variant

`TOPOLOGY_COVERAGE=1` selects the distinct `shader-capture-topology-32-uses`
XBE title and ISO. It retains the same pixel/vertex programs, 32 tile uses,
constants, animation, two authored 4-by-4 textures, and six stored entries per
tile. Tile `i` selects guest mode `5 + i % 6`:

| Guest mode | Primitive | Uses per frame | Actual submitted vertices per use |
| --- | --- | ---: | ---: |
| 5 | Triangles | 6 | 6 |
| 6 | Triangle strip | 6 | 4 |
| 7 | Triangle fan | 5 | 4 |
| 8 | Quads | 5 | 4 |
| 9 | Quad strip | 5 | 4 |
| 10 | Polygon | 5 | 4 |

That is 32 commands and 140 submitted vertices per frame. Every range still
starts at `tile * 6`; the unused two stored entries in each four-vertex draw
are never included in `DRAW_ARRAYS`. Strip and quad-strip corner order is
bottom-left, bottom-right, top-left, top-right. Fan, quad, and polygon order
follows the perimeter. Each use fills the same tile rectangle. The ordinary
build continues to submit 32 six-vertex triangle-list commands (192 vertices).

Build with a writable isolated nxdk copy and its compiler tools on `PATH`:

```sh
PATH=/tmp/pr259-nxdk/bin:$PATH make NXDK_DIR=/tmp/pr259-nxdk
PATH=/tmp/pr259-nxdk/bin:$PATH make NXDK_DIR=/tmp/pr259-nxdk TOPOLOGY_COVERAGE=1
```

The variant XBE is placed in `bin-topology/`; switching modes recompiles only
this fixture's object and preserves SDK library builds. Override `OUTPUT_DIR`
and `GEN_XISO` to stage variant artifacts outside the source tree. Rebuild
without the flag to restore the default fixture's object/executable. Guest
primitive and count constants are cross-checked against both pbkit `nv_regs.h`
and xemu `nv2a_regs.h`; quad/quad-strip corner ordering matches local
`pgraph/glsl/geom.c` assembly requirements.

Building the XBE does not establish hardware compatibility or recording/replay
correctness. Qualify each captured guest topology and its actual host topology,
raw four/six vertex count, constants, texture evidence, saved/reopened occurrence,
and original/edited output on each backend. This variant has not been tested on
Xbox hardware.
