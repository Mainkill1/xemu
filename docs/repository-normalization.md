# Repository normalization inventory — September 9, 2026

**Status: preservation and upstream setup complete; baseline selection and carry-forward main are pending.** No emulator behavior changes, branch deletion, history rewrite, or cleanup merge is included.

## Current branch identities

| Branch | Commit | Meaning now |
| --- | --- | --- |
| `upstream` | `75650bd8cd91945f7b79774e2cee0b200ca373ff` | Exact `xemu-project/xemu:master` at the audit snapshot; no local patches. Native behavior at this exact head has not been qualified here. |
| `baseline` | Not selected | No branch is created merely to label an unqualified reference. See the [selection record](performance/baseline-selection.json). |
| `main` before this documentation PR | `bd1fecb93353272dda2a810991e28945de35b665` | Retained Full-Speed tree plus accepted APU snapshot-load repair #53; broader carry-forward review remains open. |

The [preservation release](https://github.com/Mainkill1/xemu/releases/tag/archive/2026-09-09/main-before-normalization) retains a complete Git bundle and checksum, with the captured branches, tags and PR-head histories. Existing PR discussions, release assets and external captures are separate from the bundle and remain preserved at their existing locations. No working branch is being removed.

## Archive tags created

All tags below begin with `archive/2026-09-09/`. They are immutable historical anchors, not qualification labels.

| Tag suffix | Commit |
| --- | --- |
| [main-pre-apu-snapshot](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/main-pre-apu-snapshot) | `586de4f32c11001c0abe072c3530236f1571b372` |
| [main-before-normalization](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/main-before-normalization) | `bd1fecb93353272dda2a810991e28945de35b665` |
| [integration-stable-next-acceptance](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/integration-stable-next-acceptance) | `208e4596832a1f949bd63822d5ddd962c4575067` |
| [release-stable-v2-final-unvalidated](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/release-stable-v2-final-unvalidated) | `dce89f5c12125cfb4a9416a666c84994da0981e2` |
| [pr25-wait-xiso-markers](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/pr25-wait-xiso-markers) | `a2a444a85f29dab215bbd646ce1a32a690a9fd1b` |
| [pr2-gloffscreen-owned-resources](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/pr2-gloffscreen-owned-resources) | `d7223fb8a65a20488c60d86cd689dfa2a833f189` |
| [pr18-texture-repair-series](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/pr18-texture-repair-series) | `2e801ab07c4a8d0a87defd2ba70ddad3b3e1041c` |
| [pr48-ptimer-future-deadlines](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/pr48-ptimer-future-deadlines) | `cd5af56967f6d4e5c521de3d98bfe64a6872b7f9` |
| [pr59-ptimer-main-unvalidated](https://github.com/Mainkill1/xemu/tree/archive/2026-09-09/pr59-ptimer-main-unvalidated) | `8da17c3e68c525475f55f3e9d1ddda0ad9c11d5b` |

The pre-existing `archive/full-speed-e7bf825d` tag preserves `e7bf825d9cf073b38c52f7fba73efd08509913af`. The bundle also retains older archive tags, review parents and closed-PR heads.

The release also provides the [complete branch/PR inventory](https://github.com/Mainkill1/xemu/releases/download/archive/2026-09-09/main-before-normalization/branch-pr-inventory.json) and [90-commit main-content ledger](https://github.com/Mainkill1/xemu/releases/download/archive/2026-09-09/main-before-normalization/main-content-ledger.json), including exact source identities and explicitly unavailable measurement attribution.

## Important PR and branch dispositions

These are audit dispositions, not automatic changes to GitHub PR state. Historical merges into a blocked release lineage do not establish acceptance into current main.

| Disposition | PRs | Treatment |
| --- | --- | --- |
| Accepted current-main patch | #53 | APU snapshot-load repair; retain its focused correctness evidence and the unavailable old-reference timing comparison. |
| Historical merged release-line work | #47, #50 | Preserve merge history; not accepted into current main by normalization. |
| Candidate | #2, #5–#11, #14–#16, #18, #37 | Preserve actual stacked bases, implementation and evidence; no implicit merge or rebase. |
| Research | #54, #55, #57 | Preserve diagnostic/optimization investigations; no new optimization during normalization. |
| Blocked | #12, #13, #25, #51, #58, #59 | Keep outstanding qualification/performance gates explicit. #51 must not merge as cleanup. |
| Superseded for the current route | #3, #4, #22, #48 | Preserve evidence and predecessor implementations; supersession does not erase valid tests. |
| Historical | #1, #17, #21, #23, #24, #52 | Retain source, descriptions and failed/research results without promoting them to release status. |

All PR numbers in this table refer to [Mainkill1/xemu](https://github.com/Mainkill1/xemu/pulls?q=is%3Apr). `fix/main-apu-snapshot-load` is represented by merged #53. `fix/ptimer-main-qualification` remains blocked under #59. The frozen S integration and release branches diverge from current main and remain preserved. Existing long branch names and review-parent branches are not rewritten to satisfy the new naming convention.

PR #59 has native timer positive/negative controls, but not completed compatibility and fixed-work performance qualification. The legacy-baseline snapshot reload also reproduces the controller overlay tracked in [#61](https://github.com/Mainkill1/xemu/issues/61); this is not evidence that the PTIMER candidate caused it. The full-suite gates are independently tracked in [#60](https://github.com/Mainkill1/xemu/issues/60), [perf-tests #10](https://github.com/Mainkill1/xemu-perf-tests/issues/10) and [perf-tests #11](https://github.com/Mainkill1/xemu-perf-tests/issues/11).

## Why main and baseline are not silently relabeled

The audited main and upstream have common ancestor `d73326b62199c6dd952ef512947710e1333a49d3`, with 90 main-only and 17 upstream-only commits. Main's local range contains 63 source/test patches, seven diagnostic commits, 11 documentation commits, seven historical evidence commits, and the source/merge commits for #53. The existing README explicitly describes research integration and held experiments.

GitHub's commit-to-PR associations identify #53 as the only merged current-main PR in that range. That API result does not prove earlier work was never accepted elsewhere; it means normalization has not established each historical patch's intended carry-forward status. Source families need explicit keep, upstream replacement, superseded or archive-only decisions. No percentage improvement can be inferred from ancestry.

The retained diagnostic release and prior measurements remain useful for their exact source/test identities. They are not discarded because newer tests expose failures. Neither a historical report for a different binary nor an incomplete new campaign selects the next baseline automatically.

## Answers the final normalization must provide

| Question | Current answer / remaining gate |
| --- | --- |
| What does untouched upstream do? | Source is pinned above. Exact-head native behavior remains unmeasured here. |
| What exact state is the cycle baseline? | Pending a justified source and results selection. |
| What accepted changes are in main? | #53 is verified as accepted; historical source families remain in the carry-forward inventory. |
| What did each optimization change from previous main? | Record both source identities and matched results; historical missing comparisons remain explicitly unavailable. |
| What is the cumulative change versus baseline? | Unavailable until a cycle reference and matched candidate measurements exist. |
| Where does evidence survive branch deletion? | Immutable source tags/bundle, existing PR discussions, releases and pinned [test evidence](https://github.com/Mainkill1/xemu-perf-tests/pull/7); verify all links before any deletion. |

The [workflow document](repository-workflow.md) defines how these gates are maintained. This inventory does not declare the normalization complete.
