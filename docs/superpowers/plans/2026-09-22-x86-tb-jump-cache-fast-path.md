# x86 TCG Jump-Cache Fast Path Implementation Plan

> **For Codex:** Follow this plan with test-driven development. Do not advance
> from a failing verification step without explaining and fixing the failure.

**Goal:** Bypass the non-inlined common lookup call on the dominant successful
32-bit x86 jump-cache hit while preserving all existing lookup semantics.

**Architecture:** Extract the current jump-cache tag predicate into a small
internal inline helper shared by `tb_lookup` and the specialized x86 helper.
The specialized helper performs the normal no-breakpoint cache hit directly;
all misses and exceptional modes retain the existing common path.

**Tech stack:** C, QEMU TCG internals, Meson unit tests, xemu release builds,
Linux `perf`, Mainkill1/Xemu-Test-Runner.

---

## Task 1: Pin the existing cache-match contract

**Files:**

- Create: `tests/unit/test-tcg-jump-cache.c`
- Modify: `tests/unit/meson.build`
- Test: `tests/unit/test-tcg-jump-cache.c`

1. Add a focused unit test that constructs one `CPUJumpCache` entry and a
   `TranslationBlock`/`TCGTBCPUState` pair.
2. Assert an exact PC, CS base, flags, and cflags match returns the TB.
3. Add one negative assertion for each mismatching field and for a null entry.
4. Register the target in `tests/unit/meson.build`.
5. Run the focused test and confirm it fails because the shared lookup helper
   has not been introduced yet.

## Task 2: Extract and reuse the cache-hit helper

**Files:**

- Modify: `accel/tcg/tb-jmp-cache.h`
- Modify: `accel/tcg/cpu-exec.c`
- Test: `tests/unit/test-tcg-jump-cache.c`

1. Add an internal inline helper that atomically reads the indexed TB and
   applies exactly the current PC, CS base, flags, and cflags comparisons.
2. Replace the open-coded predicate in `tb_lookup` with that helper without
   changing QHT fallback, cache population, assertions, or invalidation.
3. Run the focused unit test and confirm all positive and negative cases pass.
4. Inspect the diff to ensure this task is behavior-preserving.

## Task 3: Add the specialized i32 fast hit

**Files:**

- Modify: `accel/tcg/cpu-exec.c`
- Test: `tests/unit/test-tcg-jump-cache.c`

1. In `HELPER(lookup_tb_ptr_i32)`, preserve `can_do_io` and construct the same
   state values as today.
2. When the breakpoint queue is empty, compute the existing jump-cache hash
   and call the shared lookup helper.
3. On a hit, preserve execution logging and return the cached `tc.ptr`.
4. On a miss or any active breakpoint, delegate to
   `lookup_tb_ptr_common` unchanged.
5. Run the focused test, the sanitizer-backed xemu helper tests, and the unit
   suite. Confirm no warnings or regressions.

## Task 4: Verify generated behavior and build identity

**Files:**

- No product changes expected.
- Record evidence under the campaign workspace only; do not commit private
  game data, captures, host paths, or build logs.

1. Produce an optimized Linux x86-64 build with matching debug symbols.
2. Confirm the build ID matches the debug file and inspect the helper
   disassembly to verify the hit path no longer calls the common helper.
3. Produce the matching optimized Windows x86-64 build.
4. Run privacy and diff checks before any push.

## Task 5: Native correctness and performance qualification

**Files:**

- Update the draft PR body from
  `evidence/wiki-xiso-per-test/PERFORMANCE_PR_TEMPLATE.md`.

1. Use the validated Xemu-Test-Runner build without adding debugging-only
   runner features.
2. Run interleaved baseline/candidate full-launch PGR2 cells on Steam Deck for
   Vulkan and OpenGL, retaining guest progression, frame cadence/tails, CPU,
   GPU, and complete input/build identities.
3. Re-profile the candidate's guest-vCPU thread to prove lookup cost falls and
   identify the new limiting path.
4. Run matching Windows Vulkan/OpenGL controls for correctness and regression.
5. Apply the template's sign convention and acceptance gates. Preserve
   unfavorable results and do not claim displayed FPS from guest flips.
6. If the candidate is correct and materially beneficial, commit, push only
   to `mainkill1`, open the PR with the required agent declaration, and update
   issues #162/#163 with the bounded evidence.
