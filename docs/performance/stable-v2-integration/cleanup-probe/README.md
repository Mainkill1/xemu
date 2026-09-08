# Vulkan cleanup cost probe

Source review found a concrete addition between frozen S and candidate
`57cf3f0a`: the new ownership-safe destructor calls `g_free` on all 6 × 16
payload slots. The old success path freed only populated layers/levels. For a
one-level 2D texture, the new destructor performs 95 additional null frees.
The measured candidate's optimized `upload_texture_image.isra.0` assembly
contains the unconditional nested-loop calls; `g_free` jumps to `free`.

This probe compares the extracted destructor with a proposed null-pointer guard.
**Neither the production implementation nor the test runner was changed.**
The full ownership scan must remain valid for partially populated layouts; this
is not justification to return to success-only cleanup.

## Native results

| Populated slots | Original ns/layout | Guarded ns/layout | Difference ns/layout |
| ---: | ---: | ---: | ---: |
| 1 | 277.082 | 180.755 | 96.327 |
| 8 | 484.705 | 396.188 | 88.517 |
| 96 | 3496.418 | 3491.253 | 5.165 |

Numbers are means of eight batch-level observations, alternating variant order.
Each batch allocates and frees 25,000 layouts; these timings include allocation,
zero-initialization, small payload allocations, and cleanup. They are not isolated
instruction latencies or emulator frame times. Full layouts are effectively
neutral at this measurement scale. [Every batch](native.csv) · [identity manifest](manifest.json).

At the observed sparse-layout difference, approximately 24,000 uploads per frame
would be needed to account for the original Morrowind mean-interval difference
of 2.292 ms. This is a scale estimate, not an observed upload count or proof that
the mechanism is irrelevant. The microbenchmark uses 32-byte payloads and does
not model GPU upload work, cache pressure, or game-specific allocation sizes.

The unnecessary calls are real, but this probe does **not** establish them as
the Morrowind cause. Keep integration held. Before a production optimization,
use the existing `NV2A_PROF_TEX_UPLOAD` counter to bound actual upload frequency
and attribute the remaining surface/texture preparation work. No FPS gain or
retail qualification is claimed.

## Reproduce

In the pinned Windows toolchain container from the manifest:

```sh
x86_64-w64-mingw32.static-gcc -O2 -march=x86-64-v3 -flto bench.c \
  -o cleanup-bench.exe \
  $(x86_64-w64-mingw32.static-pkg-config --cflags --libs glib-2.0)
```

Run `cleanup-bench.exe` on native Windows and capture stdout as CSV. The program
needs no arguments, game, snapshot, or emulator process. It performs a short
warmup, then the fixed alternating sequence; it does not rerun based on results.
The proposed variant is a research comparator, not a submitted production fix.
