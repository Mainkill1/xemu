# Current-upstream integration of fork PRs

This testing branch starts at upstream `master` commit `fdfb5a8f481b2f870c57080e74ec8d3a31a47053`. It combines all 16 fork PRs open when the request was made, plus merged #47 and #50. Existing fork main and the S release remain unchanged.

## Included work

| PR | Pinned head | Integration route |
| --- | --- | --- |
| [#2](https://github.com/Mainkill1/xemu/pull/2) — ENG-2026-523 Fix: release owned SDL context and hidden window | `d7223fb8a65a20488c60d86cd689dfa2a833f189` | Included through #51; no duplicate application |
| [#5](https://github.com/Mainkill1/xemu/pull/5) — ENG-2026-523: Port Vulkan color-download submission folding onto Stable S | `3f9e6b6fa379420d159a3286d5ce3b3279268540` | Included through #51; no duplicate application |
| [#6](https://github.com/Mainkill1/xemu/pull/6) — ENG-2026-425 Correctness: revalidate bound Vulkan texture memory on S | `c1462ad7d88a1d9d20c42ee70ed3661276039aba` | Included through #51; no duplicate application |
| [#7](https://github.com/Mainkill1/xemu/pull/7) — ENG-2026-430: Bound Vulkan remapped vertex uploads | `2c292414a8eac2f147deb997b12ff8bd1a6b3685` | Included through #51; no duplicate application |
| [#8](https://github.com/Mainkill1/xemu/pull/8) — ENG-2026-450: Grow Vulkan transient vertex buffers on demand | `33151b41fda28dc318da777fac12568cffa550c7` | Included through #51; no duplicate application |
| [#9](https://github.com/Mainkill1/xemu/pull/9) — ENG-2026-432: Narrow atomic PGRAPH fence read | `67178d0af1e9b7e4a4019dd5c2c77e9045ae8b3b` | Included through #51; no duplicate application |
| [#10](https://github.com/Mainkill1/xemu/pull/10) — ENG-2026-433: Bulk inline vertex and index packets on Stable S | `c4a57d4c675eba26b2132184b3dd33754217932c` | Included through #51; no duplicate application |
| [#11](https://github.com/Mainkill1/xemu/pull/11) — ENG-2026-489: Propagate Vulkan preparation failures on cumulative S | `a6533fd5726761f0561e0a810f691f87ad3bb93b` | Included through #51; no duplicate application |
| [#14](https://github.com/Mainkill1/xemu/pull/14) — ENG-2026-523 Fix: Repair bordered cubemap S3TC mips on cumulative S | `eb338262714ef0b1e23fd60a2c168300443377ac` | Included through #51; no duplicate application |
| [#15](https://github.com/Mainkill1/xemu/pull/15) — ENG-2026-523: Fix Vulkan bordered cubemap mip copy extents | `cb89434e45a5191eda85abba25812192fe1c9b21` | Included through #51; no duplicate application |
| [#16](https://github.com/Mainkill1/xemu/pull/16) — ENG-2026-523: Validate texture DMA class and target | `37823ccf18263764c82ceb6698201330a22a6c65` | Included through #51; no duplicate application |
| [#18](https://github.com/Mainkill1/xemu/pull/18) — ENG-2026-523: Validate linear texture source spans | `2e801ab07c4a8d0a87defd2ba70ddad3b3e1041c` | Included through #51; no duplicate application |
| [#25](https://github.com/Mainkill1/xemu/pull/25) — ENG-2026-523: Qualify optional Windows CPU-saving waits on S | `a2a444a85f29dab215bbd646ce1a32a690a9fd1b` | Included through #51; no duplicate application |
| [#37](https://github.com/Mainkill1/xemu/pull/37) — Add performance toggles to Settings → Advance | `1636d1ef9ffee4a1734a5187760e5805dea7c232` | Distinct delta; conflict resolution described below |
| [#47](https://github.com/Mainkill1/xemu/pull/47) — lru: skip free-node scans when the cache is full | `ff9625dddb32e9c5e60f548c5c19015f4d0a6f54` | Included through #51; no duplicate application |
| [#48](https://github.com/Mainkill1/xemu/pull/48) — nv2a: correct PTIMER deadlines, masked alarms and PLL transitions | `cd5af56967f6d4e5c521de3d98bfe64a6872b7f9` | Distinct PTIMER delta |
| [#50](https://github.com/Mainkill1/xemu/pull/50) — NV2A: balance paused snapshot saves and failure cleanup | `bab07eb3892052c5a0ff74245587f9c7e012c113` | Included through #51; no duplicate application |
| [#51](https://github.com/Mainkill1/xemu/pull/51) — Prepare the S-based release tree while preserving main history | `dce89f5c12125cfb4a9416a666c84994da0981e2` | Aggregate application and regression-test extraction |

## Integration notes

- **#47 and #50 are explicitly included.** The LRU full-cache repair and paused snapshot/failure cleanup arrived through #51. Their regression tests remain in this tree.
- #37 packet dispatch conflicts with the later #10 capacity repair. Capacity admission runs before the optimization switch, so disabling bulk dispatch cannot disable bounds checking.
- #37 native-S3TC switch keeps the later shared cubemap face-layout and border repairs.
- #48 brings the current PTIMER deadline, masked-state, PLL and save/load implementation onto this aggregate.
- Upstream DXGI presentation and SDL 3.4.16 are retained. The existing clipboard partial-window guard is still needed in the pinned SDL source and is reapplied through the new git wrap, without downgrading SDL.
- Upstream JSON dependency/CI/documentation/input-controller updates remain intact. Old release performance reports are linked from their original PRs instead of copied as apparent qualification of this branch.

## Verification status

- Local production packet/reservation harness: 24 valid modes, 48 capacity refusals, and On/Off reservation fallback cases pass under AddressSanitizer/UndefinedBehaviorSanitizer.
- Negative control: moving capacity admission under the bulk switch fails the dropped-packet assertion with the switch Off.
- Actual save/NV2A lifecycle harness: 150 attempts pass, including running/paused repeated saves and failure cleanup.
- The explicit Release A/B campaign is complete: 12 native tests, eight PGR2 runs, four Morrowind snapshot runs, and all 30 XISO renderer/group comparisons. [Full numerical report, build identities and CSVs](../upstream-release-ab/README.md).
- Integration remains draft: eight OpenGL output-hash differences need review, several XISO timings worsened, and individual tweak On/Off combinations are not qualified. The report distinguishes successful execution from performance/correctness acceptance.

## Upstream overlaps

Upstream open PRs were checked before this port. Related work remains open: [#3035 PTIMER](https://github.com/xemu-project/xemu/pull/3035), [#3010 GL S3TC](https://github.com/xemu-project/xemu/pull/3010), [#3004 buffer allocation fallback](https://github.com/xemu-project/xemu/pull/3004), [#2996 buffer sizing](https://github.com/xemu-project/xemu/pull/2996), [#2994 active LRU](https://github.com/xemu-project/xemu/pull/2994), and [#819 snapshot surface download](https://github.com/xemu-project/xemu/pull/819). This is the requested fork integration/testing branch, not a competing upstream submission.
