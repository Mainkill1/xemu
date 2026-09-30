# Instructions for Autonomous Agents & AI Assistants

This file provides mandatory instructions and guidelines for autonomous AI agents, coding assistants, and models contributing code or creating pull requests for the xemu project.

---

## PR Decisions, Performance Evidence, and Presentation

### Lead with the current recommendation

The first section of every PR must use this format, choosing exactly one
recommendation:

```markdown
## Recommendation: MERGE | HOLD | FAIL
**Summary:** What changed, the measured outcome, and the reason for this decision.
**Remaining:** None, or the specific work needed before reconsideration.
```

- **MERGE**: The tested candidate satisfies the declared correctness,
  performance, and review gates. Required evidence is complete and applicable
  to the current candidate. This is a recommendation, not permission to merge.
- **HOLD**: Required testing is missing, evidence is stale/incomparable,
  results are inconclusive, or useful improvements come with unresolved
  regressions or tradeoffs. Preserve the useful result and provide a bounded
  handoff: what improved, what worsened, and what to investigate next.
- **FAIL**: Completed, adequate evaluation identifies zero useful improvements
  in performance, correctness, or functionality, and no worthwhile direction
  remains for this candidate. This means stop pursuing it, not merely that
  one test or metric failed. Missing evidence, uncertainty, or mixed gains
  and losses are HOLD, not FAIL.

For example, p99 increasing by 10 ms while CPU falls by 10% is **HOLD** with
both measurements and a performance handoff, not FAIL. A correctness repair
is itself an improvement even without a speedup. A failed correctness check
still blocks MERGE; a useful but broken candidate remains HOLD for repair.
Record individual test failures honestly regardless of the PR recommendation.

A successful build or passing correctness suite is not performance proof.
"No speedup claimed" does not exempt runtime changes from regression testing.
A necessary correctness fix with a measured performance cost must disclose
that cost and remain HOLD until the tradeoff is explicitly accepted.

### Keep the description current and readable

Normally target 200-400 words of prose, plus compact tables and necessary
flow diagrams. Do not remove material findings to meet the length target.

Explain what changed, why it should help, what was actually measured,
and what remains unresolved. Replace stale summary text rather than
prepending a chronological progress log. Preserve historical evidence
in comments, clearly identifying superseded results.

### Show significant processing-flow changes

When significantly changing execution order, branching, caching, batching,
threading, ownership, or synchronization, include BEFORE and AFTER diagrams.

Use Mermaid or compact text flow diagrams. Show the changed decisions,
work removed or deferred, and the correctness/invalidation conditions.
Simple arithmetic or constant changes do not require decorative diagrams.

### XISO correctness AND timings are mandatory

For runtime/performance-affecting changes:

1. Identify affected XISO categories and individual test IDs, plus an
   unaffected control. Run focused comparisons during development;
   complete the broader required coverage before recommending MERGE.

2. Retrieve the actual per-test timing measurements from the runner's
   result/comparison reports after the campaign completes. Do not stop
   at status, passed counts, or comparison-eligible counts. Use the
   [XISO campaign](https://github.com/Mainkill1/Xemu-Test-Runner/blob/main/docs/XISO-CAMPAIGNS.md)
   and [result/comparison](https://github.com/Mainkill1/Xemu-Test-Runner/blob/main/docs/HASH-RESULTS.md)
   documentation for the maintained workflow.

3. Report baseline time, candidate time, absolute difference, percentage
   improvement, and correctness for the relevant timed tests. Separate
   OpenGL and Vulkan. Include affected cases, controls, and material
   regressions, not only favorable rows. Use a compact table:
   `Test ID | Backend | Before | After | Time difference | Improvement % | Correctness`.

4. Preserve the full per-test comparison in linked evidence. A total
   suite duration or "160/160 passed" cannot replace individual timings.
   Expand truncated/paged reports before claiming complete coverage.
   Structural parent groups are not independent timing samples.

5. Use matched test/catalog revisions, fixed-work settings, host,
   renderer, build configuration, and cache conditions. Record repetitions,
   statistic, units, and comparison eligibility. Use balanced A/B runs;
   do not call small differences improvements without assessing variation.

6. Missing required timings mean HOLD. Failed, incomplete, or incomparable
   runs remain visible and cannot establish a speedup. If a test has no
   valid timing contract, mark it NOT COMPARABLE and explain the alternative
   measurement; never invent timing data or silently omit the gap.

Label suite wall time, individual test work time, frame time, and guest
cadence accurately. They are different measurements. When changing
emulated clocks/timers, guest-reported time alone cannot prove host
performance improvement.

Use "Improvement %" consistently: positive is better, negative is worse.
For time/cost: `100 * (baseline - candidate) / baseline`.
For throughput: `100 * (candidate - baseline) / baseline`.
A zero baseline has no meaningful percentage.

### Bind evidence to the actual candidate

Identify the exact parent/reference and tested candidate commits.
Retain executable hashes, runner revision, XISO/catalog identity,
settings, and run IDs in the linked manifest.

Keep the fixed campaign baseline unchanged. Distinguish candidate-versus-
parent results from cumulative results against the fixed baseline.

After a new push, reassess evidence applicability. Older measurements
are not automatically current-head proof; verified source/tree equivalence
may be documented rather than rerunning unchanged product code.

Documentation-only changes may state "Performance: N/A" with a reason.
Runtime changes require measurements even when their purpose is correctness.

### Keep evidence with the owning emulator PR

The xemu PR contains the current decision and compact result summary.
Detailed per-test tables, raw measurements, logs, manifests, and captures
belong in its comments or durable linked evidence storage.

Never create evidence-only PRs or evidence-storage commits in
Xemu-Test-Runner or xemu-perf-tests. Those repositories receive actual
runner/test changes only. Linking a genuine dependency PR is allowed.

Before recommending MERGE, verify: current decision; applicable CI;
before/after correctness; required XISO timings; representative workload
and resource checks; disclosed regressions; accessible evidence; and
flow diagrams where needed.

---

## 1. Strict Adherence to Project Standards

All contributions must strictly comply with the guidelines defined in [CONTRIBUTING.md](CONTRIBUTING.md).

---

## 2. Mandatory Agent Declarations

When submitting code or opening a pull request generated with or assisted by an agent:

1. **PR Description Declaration**:
   - The pull request description **must** include an explicit declaration stating which AI agent and model were used to generate or assist with the change.
   - Example:
     ```markdown
     > **Agent Declaration**: This pull request was created with assistance from [Agent Name / Model Name].
     ```

---

## 3. Scoping & Granularity Guidelines

To produce high-quality, easily reviewable pull requests, agents must observe the following constraints:

- **Single Responsibility**: Each pull request must address exactly one bug fix, hardware improvement, or specific feature. Never bundle multiple independent bug fixes, features, or cleanups into a single commit or pull request. Break independent changes into separate, logically sequenced PRs.
- **Minimal Change**: Touch only the files and lines necessary to accomplish the stated task. Do not refactor surrounding functions or reorganize include headers unless explicitly requested.
- **Verify Against Upstream**: Always ensure the branch is rebased on the latest upstream `master` and that changes do not stomp on or duplicate existing open PRs.

---

## 4. Verification & Testing

- **Compilation**: Verify that all modified files compile without warnings or errors.
- **Emulation Accuracy**: Do not hallucinate register definitions, bitfields, or hardware behaviors. Cross-reference existing implementations under `hw/xbox/` or verified hardware documentation.
- **Test Coverage & Parity**: Whenever altering hardware emulation (NV2A, APU/DSP, MCPX, memory controller, etc.), provide or suggest a test XBE that can be run on both bare-metal Xbox hardware and xemu to validate behavior.

---

## 5. Agent Pre-Submission Checklist

Before finalizing any commit or pull request, ensure:
- [ ] Commit message uses `<subsystem>: <short description>` followed by a detailed explanatory body.
- [ ] `clang-format` is applied to new files, and existing code style is respected.
- [ ] No unrelated formatting or refactoring changes are included.
- [ ] The pull request description includes the agent/model declaration.
- [ ] Existing open pull requests have been searched to avoid duplicating work.
- [ ] The PR starts with one MERGE/HOLD/FAIL recommendation, a short result summary, and remaining work; mixed benefits and regressions are HOLD, not FAIL.
- [ ] Required XISO per-test before/after timings and correctness are reported, including controls, material regressions, and the full linked comparison; pass/fail counts alone are insufficient.
- [ ] Evidence identifies the tested candidate, reference, runner, workload/settings, and run IDs; missing or stale required evidence prevents MERGE.
- [ ] Significant processing-flow changes have BEFORE/AFTER diagrams.
- [ ] The description summarizes the current head; historical/raw evidence stays with the owning xemu PR, not in evidence-only test-runner/test-suite PRs or commits.
