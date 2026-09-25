# Issue 198: one focused learned-fallback readiness repair

**Active direction: one main-based implementation candidate, followed by targeted evidence. Not a proven game fix.**

Tracks [#198](https://github.com/Mainkill1/xemu/issues/198) and [#203](https://github.com/Mainkill1/xemu/pull/203). This replaces the previous A-H implementation chain. The owner approved reducing scope; Continue/blackout remains an optional future feature, not a prerequisite.

## What validation establishes

Audited main: `134de6616e1d8f5bfe9d919a4e98e0ff7da3c928`.

A compatible full fallback executable, with usable draw resources and supported controls, takes the existing ready route and returns before synchronous graphics-pipeline creation. Therefore preparing that executable **before** the affected demand removes that demand's missing-executable construction from the foreground. This is the causal basis for the candidate, not evidence that every hitch in Batman Begins or Azurik has this cause.

The important condition is:

```text
fallback published before first demand
+ exact compatible pipeline / shader binding
+ supported controls and available draw resources
= no missing-shader/pipeline construction for that covered draw
```

Starting work at the first uncovered draw is too late to guarantee a hitch-free accurate draw. Empty history, an unseen vertex/texture/output family, resource rollover, driver contention, guest loading, and presentation delays can still stall. Report these limits instead of promising universal cold-first-launch coverage.

## Reuse what main already does

| Existing mechanism | Pinned source / implication |
| --- | --- |
| Full fallback pipeline preparation | [`hybrid-prewarm-runtime.c`](https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/hybrid-prewarm-runtime.c): cached stages -> ready binding -> exact background pipeline. Prewarm is not merely bytecode caching. |
| Ready fallback while specialization proceeds | [`draw.c`](https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/draw.c): `create_pipeline()` and `maybe_request_complete_specialization()`. Do not implement a second selector. |
| Completion-driven family wake | [`shaders.c`](https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/shaders.c): module publication calls `wake_fallback_families_for_module()`. |
| Retry deadline cleared on matching publication | [`hybrid-ready.h`](https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/hybrid-ready.h): `pgraph_vk_fallback_family_wake_for_module()` sets `retry_after_us = 0`. Do not attribute #212's separate demand-table delay to this main path. |
| No-flip completion service | [`renderer.c`](https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/renderer.c): `pgraph_vk_process_pending()` publishes modules and services retained families without requiring a successful flip. |

The previous simplified proposal still duplicated some of this existing work. That duplication is explicitly removed here.

## The actual gap to repair

[`hybrid-prewarm.c`](https://github.com/Mainkill1/xemu/blob/134de6616e1d8f5bfe9d919a4e98e0ff7da3c928/hw/xbox/nv2a/pgraph/vk/hybrid-prewarm.c) terminally marks a candidate attempted when its cached stage artifact is missing. Main's asynchronous completion integration is fragment-only; ordinary family preparation requests the fallback fragment but cannot independently prepare an absent vertex/geometry stage. Learned replay is opportunistic, and its busy gate includes all in-use hybrid pipeline work.

Repair that existing path rather than importing a second demand subsystem:

```text
validated learned family
  -> exact pipeline already resident? done
  -> missing V / optional G / fallback F?
       request through existing bounded compiler
       retain family until completion or explicit terminal failure
  -> matching module publication wakes the existing family
  -> ready binding -> existing background pipeline builder
  -> owner-thread pipeline publication
  -> existing draw selector uses the complete fallback
```

## One candidate, not another chain

Use a clean main-based candidate. The reviewed stage-generic work in #210 (`25fb9b1ed70ccc9ce5a87863916b9c879f0a4bfc`) is a donor, not permission to carry its entire ancestry. Do not simply change the GitHub base selector and accidentally include unrelated parent commits. Preserve donor authorship and describe the extracted delta.

Scope:

1. Extend exact stage requests and completion wake to V/G/fallback-F, reusing #210's ownership and admission corrections where needed. Keep source generation on its present owner for this first candidate; time it only where necessary.
2. Retain learned families waiting for absent artifacts in the existing bounded family machinery. Queue/full-table deferral is not a permanent failure or a successful pipeline submission. Do not regenerate/requeue identical pending work on every draw. Keep real failure retries bounded.
3. Start bounded learned-family service at a safe post-initialization opportunity, after device-dependent prerequisites are established, and progress through existing owner-thread completion opportunities. Do not depend exclusively on flips. Avoid a blanket all-background-work busy veto where the existing bounded queues can safely admit work. Preserve capacity and ownership checks.
4. Leave ready selection, specialized takeover, rendering side effects, cache formats, worker count/priority, presentation, and defaults unchanged. Prewarm stays selected where it is already selected; no new menu or default flip.

Primary files: `vk/shaders.c`, `vk/draw.c`, `vk/hybrid-prewarm.c`, `vk/hybrid-prewarm-runtime.c`, `vk/hybrid-family.c`, `vk/hybrid-ready.h`, `vk/renderer.c`, and narrowly required declarations/tests. Touch only files required by the final delta; this is not a mandate to change all of them.

No new general demand table, scheduler framework, readiness database, cache tier, worker pool, or draw-omission lifecycle. In-flight pipelines must keep their referenced modules/layouts alive. Validate exact keys, current-device history eligibility, generation/ticket and owner-thread publication. Retired/cancelled work must not publish.

### Lead-time stop condition

Earlier asynchronous service is useful only if it gets ahead of the affected draws. Measure publication versus first demand. If the required family still first appears at the demanding draw, or earlier service remains too late, this candidate has not solved that event. Do not conceal it with skipped rendering.

A compile-before-play wait for known families is a separate explicit tradeoff if later evidence requires it. Do not silently add an unbounded startup drain. Count added initialization time separately from any reduced interactive hitches.

## Small proof before broader testing

Start with 2-4 verified families, not a mandatory 17/96-case corpus or queued-promotion counter. Reuse [xemu-perf-tests #44](https://github.com/Mainkill1/xemu-perf-tests/pull/44) tooling, but add/select a **visible, no-omission** readiness profile; its current safe-omission/promotion pack is not this candidate's acceptance test.

| Test | Required observation |
| --- | --- |
| Train, clean exit, restart with retained history | Same exact family is recognized and prepared early. |
| Retained history, selectively absent SPIR-V artifact, caching enabled | Missing V/G/F work actually runs off-thread; the family is not discarded; complete pipeline publishes. |
| New specialization sharing a prepared fallback family | Visible correct fallback frames while specialization is absent, followed by correct first specialized use. No synchronous missing-executable construction on protected draws. |
| Identical and uniform-only replay | No structural rebuild; output oracle matches. |
| Demand before readiness / empty history | Honest uncovered/Wait outcome; do not count it as a covered success. |
| Queue pressure and reset | Bounded retention, no duplicate pending builds or stale publication; existing reference behavior preserved. |

Use only the timestamps needed to connect exact family/key, first demand, publication, first fallback use and first specialized use. A slow-frame-only log cannot establish the denominator for all first uses. Capture the small targeted interval completely, with explicit drops; do not build a new all-purpose tracing system.

After the small route proof, run one reported Batman or Azurik transition in matched, order-reversed parent/candidate trials; then qualify both reported titles before closing #198. Keep diagnostics/fault injection separate from performance runs. Record exact binaries, XISO, effective settings, application cache seeds and driver identity. Driver cache state may be unknown; never relabel an empty xemu cache as driver-cold.

For the targeted transition report absolute shader-attributed stall time, hitch count, per-run maxima, guest progress and output correctness. Do not require an FPS gain in a capped scene. A useful result must survive ordinary run variance without a material warm-path regression. Reuse unaffected CI/correctness evidence; run affected integration tests on the actual candidate.

## Disposition of earlier work

- #203 is the scope/evidence index, not a product dependency.
- #210 is the stage-support donor. #209 and test-fixture repair #223 can be judged independently; neither blocks fallback preparation by definition.
- #212-#217, #219, #220, #222, #225-#227 are **parked as the old delivery chain**. Preserve code, reviews and failures. Do not finish every review suggestion or rerun every old matrix before the focused candidate.
- #211 remains useful opt-in diagnostic prior art, not a required runtime probe. `FAIL_ON_PIPELINE_COMPILE_REQUIRED` prevents compilation, not every driver/lock delay.
- #205/#206/#208 and #174 remain research leads. GPL, key reduction, identifiers and binaries enter only after an attributed residual cost justifies them.
- The known all-draw Continue failure is retained in #225. The zero-omission repair is a correctness control, not proof of a shader-stutter improvement. Blackout is deferred, not rejected as an owner option.

## Evidence from this scope validation

Source inspection was followed by 12 isolated C checks of unchanged `hybrid-ready.h` route/retry/wake function bodies at the audited main. All passed with `cc -std=c11 -O2 -Wall -Wextra -Werror`. Minimal stand-in types and a key-equality adapter were used: these are branch characterization checks, **not** a full renderer build, GPU test or test of a new fix. They confirm that ready-fallback selection, complete-specialized takeover, resource-shortage distinction and matching-module deadline wake already exist.

No new product patch, native title run, GPU correctness result or measured stutter reduction is claimed by this scope change. #198 stays open. The next implementation agent's output is one bounded candidate and its small causal test, not more architecture documents.

[Previous investigation, preserved before scope reduction](https://github.com/Mainkill1/xemu/blob/6bbd8316c6ff12639c5147315e5fe3738a55bcf4/docs/performance/issue-198-ubershader-loading.md).
