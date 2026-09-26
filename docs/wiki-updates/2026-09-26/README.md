# Wiki update package — September 26, 2026

## Publication state

**Prepared for review; not published to the live wiki.** The connected GitHub actions can edit the source repository but did not resolve the separate `Mainkill1/xemu.wiki` repository. Direct Git transport also failed in this environment. This package deliberately does not replace pages whose current content could not be fetched.

`Development-Status-2026-09-26.md` is a standalone wiki-ready page. It reconciles merged Shader Browser Stages 1–3, draft preview/profiling, merged Vulkan readiness work, the current open APU drafts and the 47-issue inventory. Source URLs are included inline.

## Safe publication

In an authorized, clean checkout of `Mainkill1/xemu.wiki`, first fetch and fast-forward to its current remote tip. Check that `Development-Status-2026-09-26.md` does not already exist, then copy the page into the wiki root. Read the current Home/navigation page and add this single link in its development section without replacing existing content:

```markdown
[Development status — September 26, 2026](Development-Status-2026-09-26)
```

Review the diff, run `git diff --check`, commit only the new page and that navigation change, and push normally. Do not force-push or copy the source repository's history into the wiki. Verify both the rendered page and its navigation link afterward. Merging this source-repository package alone does **not** publish the wiki.

## Targeted existing-page follow-through

| Existing subject | Change to make only after reading its latest text |
| --- | --- |
| Shader Browser / feature availability | Mark #237/#238/#239 merged; retain #241/#242 as drafts and preserve their backend/validation restrictions. |
| Vulkan learned fallback readiness / performance results | Reflect merged #228, bounded scope, mixed frame tails and the configuration-specific Morrowind resolution. |
| Audio / Advanced settings | Link the 12 open APU drafts. Do not advertise proposed JIT defaults, linear interpolation or linear Auto policy as accepted defaults. |
| Development / planned testing | Link the 47-issue coverage and current draft inventory; preserve exact-head and native-validation requirements. |
| Historical results / evidence index | Keep the fixed baseline, unsuccessful trials, adverse deltas, excluded runs and inherited failures intact. |

## Review coverage and limits

The connected repository-wide open-PR search returned 14 PRs. Open-issue searches returned 47 issues. Their descriptions and selected relevant merged/closed PRs were reviewed, together with the accessible wiki overview and repository workflow. The latest accessible wiki overview advertised 208 pages; many page fetches failed. This is not a verified page-by-page audit of those 208 pages or an exhaustive audit of every historical closed PR and discussion comment.

No emulator source, settings, CI, issue state or existing PR branch was modified. No emulator builds, game tests, audio tests or benchmarks were run for this documentation package. Local validation covers Markdown structure, coverage IDs, whitespace and internal package consistency only; linked remote evidence remains its authors' reported evidence.

Agent declaration: this documentation audit and package were prepared with ChatGPT / GPT-6 Astra Pro under the user's direction.
