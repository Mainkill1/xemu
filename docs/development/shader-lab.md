# Shader Lab

Shader Lab preserves xemu's experimental shader, draw-capture, and asset-investigation tooling outside the production `main` roadmap.

Canonical tracker: #313

## Branches

- `experimental/shader-lab` — active integration branch for the complete experimental tooling stack.
- `archive/shader-lab-stage3` — exact Stage 3 Shader Browser / override checkpoint from PR #239.
- `archive/shader-lab-workbench` — exact synthetic workbench checkpoint from PR #241.
- `archive/shader-lab-draw-capture` — exact captured-draw checkpoint from PR #259.
- `archive/shader-lab-asset-browser` — exact asset-browser checkpoint from PR #260.

The `archive/` refs are preservation checkpoints. Do not routinely rebase or force-move them.

## Scope

Shader Lab is intended for renderer investigation rather than normal end-user builds. It may contain:

- shader catalog and browser UI;
- generated GLSL/SPIR-V inspection;
- synthetic shader preview and workbench rendering;
- scoped shader replacement and override controls;
- captured-draw inspection;
- renderer-resource and asset discovery;
- GLB/asset extraction;
- diagnostic persistence and investigation databases;
- expensive or intrusive developer-only renderer instrumentation.

## Relationship to main

The production fork should remain focused on emulation performance, accuracy, compatibility, and low-overhead diagnostics.

Do not merge Shader Lab wholesale into `main`.

Instead, promote useful pieces as small independent PRs when they satisfy all of the following:

1. They directly support performance, accuracy, compatibility, or diagnosis of a production problem.
2. Release-build overhead is zero or clearly bounded and justified.
3. The change does not require the Shader Lab UI or its persistence layer.
4. It has a focused test/benchmark plan.
5. It can be reviewed independently of the experimental workbench.

Likely extraction candidates include shader identity tracking, compilation timers, draw counters, cache/pipeline statistics, bounded debug capture hooks, and a narrow renderer inspection API.

## History

The early Shader Browser stages were merged through PRs #237, #238, and #239. PR #268 intentionally removed those experimental stages from `main` while preserving the development branches.

The later stack continued through:

- #241 — synthetic shader workbench and external browser
- #259 — captured draw shader investigation workbench
- #260 — live captured Asset Browser and assembly extraction

These PRs are historical/review checkpoints rather than production merge targets.

## Future architecture

If a stable inspection interface emerges, the preferred long-term split is:

```text
xemu main
  |
  +-- narrow debug / inspection API
          |
          +-- lightweight performance diagnostics
          |
          +-- external xemu-inspector / Shader Lab UI
```

That keeps the emulator lean while retaining the richer GPU laboratory for targeted development.
