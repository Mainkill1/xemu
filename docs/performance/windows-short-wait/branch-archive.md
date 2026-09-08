# Branch consolidation — 2026-09-08

GitHub branches reduced from **35 to 25**. The active Windows wait lane is
[PR #25](https://github.com/Mainkill1/xemu/pull/25), branch
`validate/eng-2026-523-wait-xiso-markers`. No archived change is marked merged
or qualified by this cleanup. Historical PR discussion and evidence remain.

Each removed branch has an exact-tip tag prefixed `archive/2026-09-08/`.
Restore that tag to a branch when the item becomes active again.

| Removed branch | Preserved commit | Historical PR |
|---|---|---|
| `baseline/eng-2026-523-upstream-d733-cross-backend` | [`9a29ca4c4863`](https://github.com/Mainkill1/xemu/commit/9a29ca4c4863051294c024090cf845e500017e5c) | — |
| `evidence/eng-2026-523-pgr2-lag-issue70` | [`5833b7bf2925`](https://github.com/Mainkill1/xemu/commit/5833b7bf292572c6d6150cea34e62c379062fc5b) | — |
| `master` | [`99a8bd3b4820`](https://github.com/Mainkill1/xemu/commit/99a8bd3b4820baf8e93b8670f4424791cf8ff990) | — |
| `research/eng-2026-523-pgr2-poll-spin-attribution` | [`466741a87fb3`](https://github.com/Mainkill1/xemu/commit/466741a87fb3fb77eaf69b49be11aee9748070a1) | [#21](https://github.com/Mainkill1/xemu/pull/21) |
| `fix/eng-2026-523-ptimer-scheduling-3035-port` | [`3ca12096470a`](https://github.com/Mainkill1/xemu/commit/3ca12096470a3c6c7bf5e3476ef4f3511723378a) | [#22](https://github.com/Mainkill1/xemu/pull/22) |
| `research/eng-2026-523-pgr2-deadline-owner-attribution` | [`e4874c74ccf9`](https://github.com/Mainkill1/xemu/commit/e4874c74ccf9b7aa40450a38f9bf86642caabd18) | [#23](https://github.com/Mainkill1/xemu/pull/23) |
| `research/eng-2026-523-win-highres-wait` | [`172c4bd80a2e`](https://github.com/Mainkill1/xemu/commit/172c4bd80a2e4583e39b83190a4a6807d8ab9115) | [#24](https://github.com/Mainkill1/xemu/pull/24) |
| `evidence/eng-2026-523-pr15-cubemap` | [`fa496900e156`](https://github.com/Mainkill1/xemu/commit/fa496900e156abd893168c351cc8cbfd8a5e3755) | [#17](https://github.com/Mainkill1/xemu/pull/17) |
| `feature/eng-2026-523-s-vk-texture-pipeline-fastpath-udd8eff36` | [`4190822eb6f4`](https://github.com/Mainkill1/xemu/commit/4190822eb6f4cae65bbb57254d98290a8812af26) | [#12](https://github.com/Mainkill1/xemu/pull/12) |
| `feature/eng-2026-513-s-vulkan-display-reuse-u58cd060c` | [`971873d94048`](https://github.com/Mainkill1/xemu/commit/971873d940485b9106ac55b2be25c2a0dc18d740) | [#13](https://github.com/Mainkill1/xemu/pull/13) |

The texture pipeline fastpath remains held for its GPU-load tradeoff. Display
reuse and public cubemap evidence are parked, not rejected or merged. Research
PRs #21–#24 are superseded as active work by #25; PTIMER semantics changes are
not silently incorporated into the optional wait implementation.
