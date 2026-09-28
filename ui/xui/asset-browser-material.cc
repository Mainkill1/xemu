// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-material.hh"
#include <algorithm>
namespace xemu::asset_browser {
AssetTexture DecodeAssetTexture(const AssetPart &part, uint32_t backend,
                                int slot, uint64_t budget)
{
    AssetTexture result;
    if (!part.occurrence || !part.has_uv || slot < -1 || slot > 3) {
        result.reason = "Texture view needs captured UVs and an owned draw";
        return result;
    }
    const auto &inputs = part.occurrence->inputs;
    if (slot == -1) {
        for (size_t i = 0; i < inputs.textures.size(); ++i) {
            if (inputs.textures[i].described &&
                inputs.textures[i].metadata.bound) {
                slot = int(i);
                break;
            }
        }
    }
    if (slot < 0) {
        result.reason = "No captured texture is bound";
        return result;
    }
    result.slot = slot;
    const auto &source = inputs.textures[slot];
    const auto &meta = source.metadata;
    if (!source.described || !meta.bound || !meta.width || !meta.height ||
        meta.depth != 1 || meta.face_count != 1) {
        result.reason = "Selected texture is absent or not a supported 2D view";
        return result;
    }
    const uint64_t bytes = uint64_t(meta.width) * meta.height * 4;
    if (meta.width > 4096 || meta.height > 4096 || bytes > budget) {
        result.reason = "Texture exceeds diagnostic material budget";
        return result;
    }
    const capture::CaptureOwnedImage *image = nullptr;
    for (const auto &candidate : source.images) {
        if (candidate.face == 0 && candidate.mip_level == 0) {
            if (image) {
                result.reason = "Duplicate base texture images";
                return result;
            }
            image = &candidate.image;
        }
    }
    if (!image || image->width != meta.width || image->height != meta.height ||
        !image->rgba || image->rgba->bytes.size() != bytes) {
        result.reason = "Owned base-level texture pixels are missing";
        return result;
    }
    result.width = meta.width;
    result.height = meta.height;
    result.rgba = image->rgba->bytes;
    auto filter = [backend](uint32_t value) {
        if (backend == 2)
            return value == 1 ? 0x2601U : 0x2600U;
        return value == 0x2601 || value == 0x2701 || value == 0x2703 ? 0x2601U :
                                                                       0x2600U;
    };
    auto wrap = [backend](uint32_t value) {
        if (backend == 2) {
            if (value == 0)
                return 0x2901U;
            if (value == 1)
                return 0x8370U;
            return 0x812fU;
        }
        return value == 0x2901 || value == 0x8370 ? value : 0x812fU;
    };
    result.min_filter = filter(meta.min_filter);
    result.mag_filter = filter(meta.mag_filter);
    result.wrap_s = wrap(meta.wrap_s);
    result.wrap_t = wrap(meta.wrap_t);
    std::array<uint32_t, 4> swizzle{ 0x1903, 0x1904, 0x1905, 0x1906 };
    if (backend == 1) {
        const auto prefix =
            "capture.texture" + std::to_string(slot) + ".swizzle";
        for (const auto &reg : inputs.registers)
            for (size_t c = 0; c < 4; ++c)
                if (reg.name == prefix + std::to_string(c))
                    swizzle[c] = reg.value;
    }
    for (size_t pixel = 0; pixel < result.rgba.size(); pixel += 4) {
        const std::array<uint8_t, 4> original{ result.rgba[pixel],
                                               result.rgba[pixel + 1],
                                               result.rgba[pixel + 2],
                                               result.rgba[pixel + 3] };
        for (size_t c = 0; c < 4; ++c) {
            if (swizzle[c] == 0)
                result.rgba[pixel + c] = 0;
            else if (swizzle[c] == 1)
                result.rgba[pixel + c] = 255;
            else if (swizzle[c] >= 0x1903 && swizzle[c] <= 0x1906)
                result.rgba[pixel + c] = original[swizzle[c] - 0x1903];
            else {
                result.rgba.clear();
                result.reason = "Unsupported captured channel swizzle";
                return result;
            }
        }
    }
    result.reason =
        "Captured base texture with raw UVs; original vertex/pixel stages, "
        "mip/compare/border state and blending are not replayed";
    return result;
}
} // namespace xemu::asset_browser
