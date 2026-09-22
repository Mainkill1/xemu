# x86 TCG Jump-Cache Fast Path Design

## Objective

Reduce the cost of state-preserving 32-bit x86 static jumps that leave the
current guest page without weakening system-emulation invalidation rules.
The motivating workload is Project Gotham Racing 2 on Steam Deck, where an
optimized release profile attributes about half of the saturated guest-vCPU
thread to the translation-block lookup path.

## Evidence

The full-launch PGR2 workload reaches about 25 guest flips per second with
both Vulkan and OpenGL on the Steam Deck. A scheduler trace shows one xemu
guest-vCPU thread using about 95% of one core. A symbolized optimized release
profile of that thread reports:

- `helper_lookup_tb_ptr_i32`: 24.08%
- `lookup_tb_ptr_common`: 21.81%
- jump-cache/QHT comparison helpers: about 4%

Instruction annotation shows the common path normally succeeds in the
per-vCPU `tb_jmp_cache`; the global QHT fallback is not the dominant cost.
The helper currently materializes `TCGTBCPUState`, calls a non-inlined common
function, and pays that function's prologue and cache checks on every
cross-page static jump.

## Selected Approach

Add a dedicated fast-hit path to `HELPER(lookup_tb_ptr_i32)`, which already
receives the destination EIP, CS base, and flags from translated 32-bit x86
code.

The path will:

1. Preserve the current `can_do_io` transition.
2. Read the current cflags once.
3. When no breakpoint or execution logging requires slow handling, compute
   the existing jump-cache hash and atomically load the existing cache entry.
4. Validate the full current cache contract: PC, CS base, flags, and cflags.
5. Return the cached translated-code pointer on a hit.
6. Delegate misses and exceptional modes to the existing common lookup path.

No new cache, title identifier, game heuristic, or cross-page direct link is
introduced. Cache population and invalidation remain unchanged.

## Alternatives Considered

### Force-inline the common lookup

This is smaller syntactically but duplicates cold breakpoint, logging, and
QHT code into both helpers. It also gives the compiler less explicit control
over the high-frequency hit path and risks unnecessary code growth.

### Cross-page direct TB linking

This can avoid lookup entirely, but an unguarded link is incorrect in system
emulation: the destination virtual page may be remapped while the source TB
remains valid. A correct version needs a mapping-generation guard and
invalidation integration. It remains a possible second phase if the bounded
fast path is insufficient.

## Correctness Invariants

- The cache entry is read with the same atomic operation used by `tb_lookup`.
- A hit requires the same PC, CS base, flags, and cflags comparisons as the
  existing implementation.
- Breakpoint and execution-log modes always use the existing common path.
- A miss always uses the existing QHT lookup, population, code-generation,
  and epilogue behavior.
- Translation-block invalidation semantics are unchanged.
- The generic helper and non-x86 targets are unchanged.

## Test Strategy

Implement a small cache-match helper with focused tests for exact hits and
each mismatching tag. Add integration coverage that proves the specialized
helper selects the direct return only when breakpoint/logging gates permit
it and delegates otherwise. Run the existing unit suite and sanitizer-backed
tests before native qualification.

Native qualification will use Mainkill1/Xemu-Test-Runner and the performance
PR template:

- Steam Deck full-launch PGR2, Vulkan and OpenGL, interleaved baseline and
  candidate repeats;
- Windows full-launch PGR2 control on both renderers;
- guest progression/frame cadence, frame-time tails, process/host CPU, GPU,
  and correctness evidence;
- symbolized candidate profiling to confirm lookup cost actually falls.

The change advances only if it preserves correctness and materially improves
the Deck's steady in-game cadence. If it does not reach the 30 FPS objective,
the remaining profile determines whether to pursue guarded cross-page links
or the next independent hotspot.
