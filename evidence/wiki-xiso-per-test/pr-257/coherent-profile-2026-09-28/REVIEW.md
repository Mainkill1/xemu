# Read-only integration review

Reviewed parent35bbc3224352..aec9d7198507 and dependent childe2cb051a0b7c..
cbefd6c70ac0 using a fresh Codex/GPT-6 reviewer. No critical or important code
finding. The review audited actual initialization/reset consumers, retained
process mode, mutex/adapter order, unchanged renderer guards and typed child
persistence. Parent and child actual strict UI/related compilation completed;
full commands/output are retained in each branch's evidence directory.

## Deferred minor

`ui/xui/widgets.cc` displays two restart instructions when an available
restart-only Boolean is disabled live. Runtime requested/effective status is
correct; native UI polish can suppress the duplicate reason without removing
unavailable/restart explanations. No correctness finding was deferred.

## Decisions and limits

- Execute the supplied #93 design under existing authorization to finish these
  PRs; no new approval cycle. A wrong scope interpretation would spend work the
  user did not request; broader shader/asset runtime stayed parked.
- Retain startup-selected permissions separately from backend-effective bits.
  A wrong initialization boundary could suppress an init consumer; the review
  inspected actual retained-mode consumers and no init dependency was found.
- Combine resolver and publication in one coherent implementation commit before
  the child consumes it. Cost if wrong: less commit-level isolation; pure and
  production tests were independently exercised before the combined commit.
- Native UI/process restart/backend/gameplay behavior remains unqualified by this
  read-only review. Keep its native gate; cost of claiming otherwise is untested
  user-visible regression.
- Steam Deck/ABBA/BAAB measurement remains unqualified; keep its performance gate.
  Treating units as speedup evidence would support an invalid optimization claim.
- Full #94 override/session/counter/validation remains subsequent implementation.
  Without it, a paired-run report can lack actual setting/path/identity evidence.
- Current-head full CI/product linking is a separate gate; focused compilation
  cannot establish all platform/link configurations.
- Unchanged renderer algorithms were inspected only at integration consumers and
  guarded child placement. Full emulation/gameplay parity is a separate native
  gate, not proved by a source diff.

All are recorded acceptance boundaries, not reasons to abandon the active goal.
