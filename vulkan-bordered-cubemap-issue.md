## Reproduction

Current main reproduces Vulkan bordered-cubemap failures during the complete, frozen XISO v5 diagnostic on the Windows test box. PR #282 reproduces the same failures; they are not candidate-only failures.

- Parent: `b4d69b2440a33ad93987b269c2ecac97c45367bc` product tree, packaged at `59e5a983` (test-only differences).
- Candidate: `3ba880bb4ed1d319cbb10bf2b29c9cf25ffa3aa6`.
- Parent executable SHA256: `947949c8cd540005290310569775962e383d3fb446447b9bce1dfdd8bd662b43`; candidate: `97e1ab6b65023f547f881d3396ba3bf212372a4102a2799a9470419bb9d63a33`.
- Windows x64, Ryzen 9 6900HX, RTX 3070 Ti Laptop, NVIDIA 581.95 (Vulkan reports 581.380.0), Vulkan shared-external-memory/DXGI presentation.
- Runner `0.2.0+3aa5e059d52339bf4b14725260e124322c30beca`; ISO `0a98e667d56b16dd91f028ef09f9e535199c006ca3800b5b54238799a4436373`; catalog `f8636cdd127be157dc1c55742c32e3ffc907cb4f16e812e0bfc342b83b7660bd`.
- Effective application config Vulkan, 64 MiB, scale 1, no vsync, DSP JIT, disabled shader cache; guest work uses 3 warmups, multiplier 4, per-iteration GPU completion. Suite name includes OpenGL historically, but the frozen campaign explicitly uses application config and stderr confirms Vulkan.

### Failure A: bordered cubemap chunk, device loss

Chunk 17 selects unbordered DXT1 and bordered RGBA8/palette/R6G5B5 cases for sizes 1/2/4/8. Both variants terminate with exit 3:

```
vk_result = -4
Assertion failed: vk_result == VK_SUCCESS && "vk check failed",
file ../hw/xbox/nv2a/pgraph/vk/command.c, line 117
```

`VK_ERROR_DEVICE_LOST` occurs after the retained complete leaf `texture_cubemap_fallback.bordered_palette_size1` reports guest FAIL, 30/30 failed samples, all six faces in the failure mask, observed RGB565 all zero. `guest/results.txt` is truncated JSON; no full chunk result is claimed.

Parent run `20261004-134121521-809f922afec845b6baab26f19990a695`; candidate `20261004-135444062-3daaa74e87d74630a0fb85716fed23b0`.

### Failure B: isolated bordered converted format, divide exception

Chunk 18 contains only `texture_cubemap_fallback.bordered_r6g5b5_size8`. Both variants crash with Windows `0xC0000094` (integer divide by zero), exit `-1073741676`, before a complete leaf is retained.

Parent run `20261004-134152914-f0a3f7594bc04d749a18905d5f89ee3b`; candidate `20261004-135514321-c5f81ae2fd864eeeb9351fbbf3aba3bc`. The runner captured dumps but automated stack analysis is unavailable because `cdb.exe` is absent. No crash-stack root cause is claimed yet.

## Source lead and next repair

`vk/texture.c:get_texture_layout()` still contains explicit uncropped bordered-cubemap paths: it substitutes base-level `s.width/s.height`, leaves the crop FIXME, and halves physical level dimensions separately. OpenGL's #256 crop correction is not a Vulkan repair. This is a plausible layout/converted-size lead, not a proved location for the divide exception.

Reproduce the isolated converted-format case with symbols; check every mip's physical/logical extents, converted texel size and bounded source crop; validate palette and RGBA8 output; run native Vulkan with validation on Windows/Deck. Keep this texture correction separate from #282's depth/stencil ownership and #290's depth aliases. Preserve all leaves and failure evidence; do not replace pinned oracles with the failing output.

Numeric evidence is being retained on the isolated `evidence/pr282-qualification-20261004` branch, excluded from main. Current full raw/timing evidence will be appended to that branch after Deck acquisition completes. This report establishes inherited failures and exact reproduction identities, not an accepted performance comparison (Windows driver cache is uncontrolled).
