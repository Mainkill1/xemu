# PR #11 mapped-allocation failure follow-up

Status: **PASS**

- Previous head: `021bc7293ab534c10d311761ca975985ab50a48c`
- Final head: `a6533fd5726761f0561e0a810f691f87ad3bb93b`
- Official Windows Release SHA-256: `a8671f73923a8f0ed1a5e4beee80821f0421d01ff1dff048c4fc126f62a707aa`
- Unit SHA-256: `f070ecd4553aea01268975591c537274290c5de2ebfe98556aa664b58e1941dc`

The final audit found that surface staging treated map failure as fatal and ignored invalidate failure. Texture staging likewise treated map failure as fatal and ignored flush failure. An invalidate failure could copy unqualified bytes to guest VRAM and clear authoritative-state flags. A flush failure could submit an unqualified upload, then commit the new texture hash and clear its retry hint.

The repaired paths check map, invalidate, and flush results. A scoped guard unmaps exactly once on every return after a successful map. Surface failure returns through the existing download outcome, retaining `download_pending` and `draw_dirty`. Texture failure returns through preparation, retaining the previous hash and setting `possibly_dirty` for retry. Decoded texture levels remain owned by `g_autoptr(TextureLayout)`, so the same failure return releases every populated level.

## Deterministic failure injection

The exact-head Windows unit ran seven cases:

1. surface download failure retains authoritative state;
2. texture upload failure retains the prior hash and retry hint;
3. map failure performs no unmap;
4. post-map failure performs exactly one unmap;
5. success performs exactly one unmap;
6. surface invalidate failure combines cleanup with authoritative-state retention;
7. texture flush failure combines cleanup with hash/retry retention.

Result: **7/7 PASS**.

## Production-path checks

The same exact Release executable ran two real Vulkan workloads:

| Workload | Result | Framebuffer FNV-1a64 | VUIDs |
|---|---|---|---:|
| `SurfaceRendering::XemuSurfaceDownloadPath` | PASS | `64a30332bf7ebd84` | 0 |
| `PipelineTextureSwitch::pipeline.texture-switch` | PASS | `8ae05d31fb00c325` | 0 |

These one-shot cells validate normal operation after the repair. Their timing is excluded from performance claims because live guest markers were unavailable. The complete earlier matched XISO and retail performance evidence remains in the parent report.

Decision: **Accept.** The previously missing map/coherency failures now reach the operation result, mapped ownership is unwound exactly once, authoritative/retryable state is retained, and the exact repaired Release head passes both affected production paths.
