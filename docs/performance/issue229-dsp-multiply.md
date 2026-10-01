# Retained C DSP multiply: independent design and validation

Parent: `ee5ce48b48784f999af374c1452003f8b2b1230f`, accepted fork main on 2026-10-01.

## Design before implementation

The retained interpreter builds a fractional product with four 12-bit partial multiplies, carry handling, a multiword shift and optional multiword subtraction. Its callers pass signed 24-bit register values and a plus/minus selector. Replace that arithmetic only: decode the low 24 bits as signed values, multiply in a signed 64-bit domain, optionally negate, then shift in an unsigned domain and split the existing 8/24/24 result words. The largest magnitude is 2^46 before fractional scaling, so neither signed multiplication nor negation can overflow. There is no need for a runtime overflow check. Keep every instruction's accumulation, rounding, saturation, flags and dispatch unchanged.

Will Bonnett / Synkronicity's [Symphony multiplier work](https://github.com/Synkronicity/Xemu-Symphony/commit/6927121a40ef9df78eeff79ea031f0771e62c516) supplies the optimization idea. The third-party implementation was not inspected or copied; this change is independently written from the issue's arithmetic contract and the target's existing code. Preserve the target's GPL and copyright headers and add source-adjacent inspiration attribution. No AGU, saturation, dispatch or backend-policy changes are included.

The default remains `use_dsp_jit=true`; this helper runs in the retained C backend. Upstream #3047's proposed interpreter removal is still open. Kernel gains are bounded by actual C-backend use and must not be described as JIT or whole-game gains.

## Maintained checks

`tests/unit/test-xbox-mcpx-dsp-mul.c` includes the real interpreter translation unit to reach the private helper without a new production interface. Its arithmetic oracle uses bit-serial unsigned multiplication, not the proposed native multiplication. The boundary/random corpus matches issue #229 and includes high-bit masking checks. Real instruction replay covers all 128 parallel MPY/MPYR/MAC/MACR variants, 121 operand pairs and three scaling modes; its register/flag/PC/cycle digest is pinned to the unchanged parent. Negative controls remove fractional scaling or swap result words and must fail.

The same executable retains opt-in natural helper, matched indirect-call helper, MAC-only and mixed multiply-instruction benchmarks. Random helper inputs are prepared outside timing; instruction lanes use fixed synthetic register values. Each result contributes to a consumed checksum. Natural helpers can inline; the matched-call lane forces both revisions through an identical volatile function pointer. The instruction lanes include real fetch, dispatch, accumulation, rounding, flags and PC handling. Equal compilation options and ABBA/BAAB plus A/A repetitions are required; no latency threshold is a correctness assertion.

Native C-versus-C game qualification and a separate JIT-versus-JIT control remain required before readiness. The existing Deck Mesa qualification blocker does not affect local arithmetic/instruction tests, but it prevents a qualified native game speedup claim until the runner is upgraded.
