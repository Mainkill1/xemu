# Advance settings

Settings -> Advance exposes independent boolean toggles. There is no top-bar
menu for these options. New configurations enable
all options. Explicitly saved Off values are preserved. Graphics options are
grouped by backend; Windows CPU-saving waits are shown only on Windows.

| Setting | On | Off | Apply |
| --- | --- | --- | --- |
| Reduce CPU usage while waiting | Interruptible Windows short waits | Existing short busy-wait path | Next wait |
| Process vertex packets in bulk | Batch eligible non-incrementing packets | Scalar packet processing | Next packet |
| Fast GPU fence polling | Atomic exact 32-bit fence read | Locked read; atomic publication remains | Next read |
| Combine color downloads with rendering | Fold eligible color downloads into the active submission | Separate download submission | Next download |
| Upload only used vertex ranges | Skip unused leading remapped vertices | Copy from vertex zero | Next repack |
| Grow transient buffers to fit batches | Grow using the pre-flush batch requirement | Reuse drained storage; grow only when a single draw needs more | Restart xemu |
| Upload compressed textures directly | Native OpenGL S3TC upload for eligible textures | CPU S3TC decoding | Restart xemu |

Buffer modes both start with the existing 8 MiB inline buffer pair. Off does not
restore the historical oversized allocation, remove alignment checks, or disable
capacity required for a single draw. Restarting starts both modes from a known
allocation state. Native S3TC selection remains fixed for the process lifetime
so cached textures and mip chains cannot mix upload policies after a menu edit.

The UI saves choices through the existing configuration system. Workers read an
atomic active-options snapshot instead of the mutable configuration. Restart-only
changes show a pending message and are not published to workers during execution.
Defaults are applied by xemu after configuration loading; the generic QEMU wait
API retains its legacy initialization behavior until xemu applies its selection.

## Open PR audit

Reviewed all 16 open PRs on 2026-09-08, including their changed-file patches.
The implementation extends PR #25 and is reconciled with parent
`a2a444a85f29dab215bbd646ce1a32a690a9fd1b`. This preserves the production
marker-receiver cleanup from `a0bb1187b4f49e7a790c770b0ced487d32eca99a`.
The original slider implementation used parent `09d5c165fcded5801297c6033d6519bb35899f8e`,
which contains the menu introduced by `0edb216c23ab071042bb26005267d0c3816337cd`.
It is a follow-up to that branch, not a merge of the different PR base branches.

| PR | Decision |
| --- | --- |
| [#1](https://github.com/Mainkill1/xemu/pull/1) PCI report completions | Required correctness; no toggle |
| [#2](https://github.com/Mainkill1/xemu/pull/2) SDL resource cleanup | Required ownership cleanup; no toggle |
| [#3](https://github.com/Mainkill1/xemu/pull/3) Query budgets and transactional draw preparation | Required correctness; no toggle. Its broader mirrored performance report is not the PR's patch. |
| [#4](https://github.com/Mainkill1/xemu/pull/4) Zeta transfer-source usage | Required Vulkan usage flag; no toggle |
| [#5](https://github.com/Mainkill1/xemu/pull/5) Color-download folding | Independent control with separate-submit fallback |
| [#6](https://github.com/Mainkill1/xemu/pull/6) Texture memory revalidation | Required freshness/retry behavior; no toggle |
| [#7](https://github.com/Mainkill1/xemu/pull/7) Bounded remapped uploads | Independent used-range control |
| [#8](https://github.com/Mainkill1/xemu/pull/8) Transient buffer growth | Independent batch-growth policy; retain mandatory capacity checks |
| [#9](https://github.com/Mainkill1/xemu/pull/9) Fence-read fast path | Independent control; keep writer publication and ordering |
| [#10](https://github.com/Mainkill1/xemu/pull/10) Bulk vertex/index packets | Independent control; retain scalar capacity checks and trace behavior |
| [#11](https://github.com/Mainkill1/xemu/pull/11) Vulkan failure propagation | Required correctness and ownership; no toggle |
| [#14](https://github.com/Mainkill1/xemu/pull/14) OpenGL S3TC and cubemap mips | Gate only native S3TC uploads; retain the layout, face-stride, and border-crop repairs in both modes |
| [#15](https://github.com/Mainkill1/xemu/pull/15) Vulkan bordered cubemap extents | Required copy extents; no toggle |
| [#16](https://github.com/Mainkill1/xemu/pull/16) Texture DMA contract | Required descriptor validation; no toggle |
| [#18](https://github.com/Mainkill1/xemu/pull/18) Linear texture source span | Required source bounds; no toggle |
| [#25](https://github.com/Mainkill1/xemu/pull/25) CPU-saving waits | Reuse the existing runtime selector; default the xemu setting to On |

PR #14's four changed files are carried forward from reviewed head
`eb338262714ef0b1e23fd60a2c168300443377ac` because the menu branch does not yet
contain its upload optimization or the associated layout repairs. Its existing
texture-layout tests are retained. Other correctness PRs are not imported or
reverted by these controls.

## Validation

### Parent reconciliation (#45)

The updated parent removes 289 lines of validation-only marker definitions,
receiver, and registration from `hw/xbox/xbox.c` and `hw/xbox/nv2a/debug.h`.
Both files match the cleaned parent exactly; no tweak implementation is changed
by the reconciliation. The merged parent also supplies its published reports.

After reconciliation, the production packet/reservation driver passed all 24
packet modes and On/Off reservation fallbacks. The Windows-wait API-double
failure/ownership tests and default/enable/disable interruption checks passed.
These are host-side functional checks, not native Windows gameplay or timing
qualification. A new full application build and native qualification remain
pending. Earlier build and performance evidence does not qualify this head.

### Original slider implementation (`ca94054f`)

- `test-xemu-tweaks-config`: actual generated configuration and runtime adapter;
  default On, saved Off round-trip, old configuration migration, live switching,
  independent selection, and restart-only changes.
- `python3 tests/unit/test-xemu-tweak-paths.py`: production packet dispatch and
  reservation functions with API doubles, including scalar/traced/incrementing
  cases and single-draw capacity with growth disabled.
- Existing packet, Windows wait, texture-layout, vertex-staging and buffer-size
  tests remain applicable in both modes.

Validation of this change: Windows application cross-build passed using toolchain
`ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2`.
The configuration/runtime test plus 10 texture-layout, 7 vertex-staging,
3 buffer-size and 10 Windows-wait tests passed under Wine. The packet/reservation
driver and existing Windows-wait API-double tests passed with ASan/UBSan.

These controls are not a performance qualification. Native Windows game captures,
both renderers, live slider interaction, repeated texture-cache use, and all
seven options individually Off still need game testing before release. PR #25's
earlier measurements are context, not measurements of this combined build.
