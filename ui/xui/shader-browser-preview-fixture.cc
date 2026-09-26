// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-adapter.hh"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace xemu::shader_browser {
const char *PreviewFixtureProfileName(PreviewFixtureProfile profile)
{
    static const char *names[] = {
        "Flat Color",          "UV Gradient",      "Checker / UV Grid",
        "Alpha Gradient",      "Multi-texture",    "Normal-like Direction",
        "Cubemap / Direction", "Fog / Depth Ramp", "All Inputs Diagnostic"
    };
    unsigned i = static_cast<unsigned>(profile);
    return i < 9 ? names[i] : "Unsupported";
}
PreviewSyntheticFixture MakePreviewFixture(PreviewFixtureProfile profile)
{
    PreviewSyntheticFixture f;
    f.profile = profile;
    f.textures.fill(profile);
    f.corner_colors.fill({ 255, 255, 255, 255 });
    f.texture_texels.fill({ 180, 90, 40, 255 });
    if (profile == PreviewFixtureProfile::MultiTexture ||
        profile == PreviewFixtureProfile::Diagnostic)
        f.textures = { PreviewFixtureProfile::UV,
                       PreviewFixtureProfile::Checker,
                       PreviewFixtureProfile::Alpha,
                       PreviewFixtureProfile::Normal };
    if (profile == PreviewFixtureProfile::Fog ||
        profile == PreviewFixtureProfile::Diagnostic) {
        f.fog = 1;
        f.fog_color = { 0.2f, 0.4f, 0.8f, 1 };
    }
    if (profile == PreviewFixtureProfile::Alpha)
        f.alpha_reference = 128;
    return f;
}
PreviewTexturePixels GeneratePreviewTexture(const PreviewSyntheticFixture &f,
                                            size_t stage)
{
    PreviewTexturePixels pixels{};
    if (stage >= 4)
        return pixels;
    const auto profile = f.textures[stage];
    for (size_t face = 0; face < 6; ++face)
        for (size_t y = 0; y < 8; ++y)
            for (size_t x = 0; x < 8; ++x) {
                const uint8_t u = x * 255 / 7, v = y * 255 / 7;
                std::array<uint8_t, 4> c{ u, v,
                                          static_cast<uint8_t>(40 + stage * 60),
                                          255 };
                switch (profile) {
                case PreviewFixtureProfile::Flat:
                    c = f.texture_texels[(y >= 4) * 2 + (x >= 4)];
                    break;
                case PreviewFixtureProfile::UV:
                    break;
                case PreviewFixtureProfile::Checker:
                    c = ((x / 2 + y / 2) % 2) ?
                            std::array<uint8_t, 4>{ 255, u, 20, 255 } :
                            std::array<uint8_t, 4>{ 20, 40, v, 255 };
                    break;
                case PreviewFixtureProfile::Alpha:
                    c = { 255, 80, 30, u };
                    break;
                case PreviewFixtureProfile::MultiTexture:
                    c = { static_cast<uint8_t>(stage == 0 ? 255 : u / 3),
                          static_cast<uint8_t>(stage == 1 ? 255 : v / 3),
                          static_cast<uint8_t>(stage >= 2 ? 255 : 30), 255 };
                    break;
                case PreviewFixtureProfile::Normal:
                    c = { u, v, 220, 255 };
                    break;
                case PreviewFixtureProfile::Cubemap: {
                    static const std::array<uint8_t, 4> faces[] = {
                        { 255, 40, 40, 255 }, { 40, 255, 255, 255 },
                        { 40, 255, 40, 255 }, { 255, 40, 255, 255 },
                        { 40, 40, 255, 255 }, { 255, 255, 40, 255 }
                    };
                    c = faces[face];
                    break;
                }
                case PreviewFixtureProfile::Fog:
                    c = { u, u, u, 255 };
                    break;
                case PreviewFixtureProfile::Diagnostic:
                    c = { u, v, static_cast<uint8_t>(face * 40),
                          static_cast<uint8_t>((x + y) * 255 / 14) };
                    break;
                }
                std::copy(c.begin(), c.end(),
                          pixels.begin() + face * kPreviewTextureFaceBytes +
                              (y * 8 + x) * 4);
            }
    return pixels;
}
bool SuggestPreviewFixture(const CanonicalRecipe &recipe,
                           PreviewFixtureProfile *profile, std::string *error)
{
    RecipeInspection inspection;
    if (!profile || !InspectCanonicalRecipe(recipe, &inspection, error))
        return false;
    if (inspection.stage != Stage::Pixel) {
        if (error)
            *error = "Fixture suggestions require a pixel recipe";
        return false;
    }
    bool cube = false, alpha = false, fog = false;
    unsigned stages = 0;
    for (const auto &field : inspection.fields) {
        if (field.name.find("tex_cubemap[") == 0 && field.value == "true")
            cube = true;
        if ((field.name.find("alphakill[") == 0 ||
             field.name == "alpha_test") &&
            field.value == "true")
            alpha = true;
        if (field.name == "fog_enable" && field.value == "true")
            fog = true;
        const bool combiner = field.name.find("rgb_inputs[") == 0 ||
                              field.name.find("alpha_inputs[") == 0 ||
                              field.name == "final_inputs_0" ||
                              field.name == "final_inputs_1";
        if (field.name == "shader_stage_program" || combiner) {
            uint32_t value = 0;
            auto parsed =
                std::from_chars(field.value.data(),
                                field.value.data() + field.value.size(), value);
            if (parsed.ec != std::errc() ||
                parsed.ptr != field.value.data() + field.value.size())
                return false;
            if (combiner) {
                // Each NV2A combiner input byte stores its register in bits
                // 0..3.
                for (unsigned i = 0; i < 4; ++i)
                    if (((value >> (8 * i)) & 15) == 3)
                        fog = true;
            } else
                for (unsigned i = 0; i < 4; ++i)
                    if ((value >> (5 * i)) & 31)
                        ++stages;
        }
    }
    *profile = cube       ? PreviewFixtureProfile::Cubemap :
               alpha      ? PreviewFixtureProfile::Alpha :
               fog        ? PreviewFixtureProfile::Fog :
               stages > 1 ? PreviewFixtureProfile::MultiTexture :
               stages     ? PreviewFixtureProfile::UV :
                            PreviewFixtureProfile::Diagnostic;
    return true;
}
void ApplyPreviewOutputChannel(PreviewChannel channel,
                               std::vector<uint8_t> *rgba)
{
    const int component = PreviewChannelComponent(channel);
    if (component < 0 || !rgba)
        return;
    for (size_t i = 0; i + 3 < rgba->size(); i += 4) {
        const auto value = (*rgba)[i + component];
        (*rgba)[i] = (*rgba)[i + 1] = (*rgba)[i + 2] = value;
        (*rgba)[i + 3] = 255;
    }
}

bool RenderPreviewDiagnostic(PreviewChannel channel,
                             const PreviewSyntheticFixture &fixture,
                             uint32_t width, uint32_t height,
                             std::vector<uint8_t> *rgba, std::string *error)
{
    if (!PreviewChannelIsDiagnostic(channel)) {
        if (error)
            *error = "Unsupported diagnostic channel: " +
                     std::string(PreviewChannelProvenance(channel));
        return false;
    }
    if (!rgba || !width || !height || width > kPreviewFullExtent ||
        height > kPreviewFullExtent) {
        if (error)
            *error = "Diagnostic extent exceeds private preview bounds";
        return false;
    }
    rgba->resize(size_t(width) * height * 4);
    const bool texture =
        channel >= PreviewChannel::T0 && channel <= PreviewChannel::T3;
    PreviewTexturePixels texels{};
    if (texture)
        texels = GeneratePreviewTexture(
            fixture, static_cast<size_t>(channel) -
                         static_cast<size_t>(PreviewChannel::T0));
    auto byte = [](float value) {
        return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255 + 0.5f);
    };
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const float u = (x + 0.5f) / width, v = (y + 0.5f) / height;
            std::array<float, 4> color{ 0, 0, 0, 1 };
            float alpha = fixture.colors[0][3] *
                          ((1 - u) * v * fixture.corner_colors[0][3] +
                           u * v * fixture.corner_colors[1][3] +
                           (1 - u) * (1 - v) * fixture.corner_colors[2][3] +
                           u * (1 - v) * fixture.corner_colors[3][3]) /
                          255;
            if (channel == PreviewChannel::UV) {
                color[0] = u * fixture.uv_scale[0] + fixture.uv_offset[0];
                color[1] = v * fixture.uv_scale[1] + fixture.uv_offset[1];
            } else if (channel >= PreviewChannel::D0 &&
                       channel <= PreviewChannel::B1) {
                color = fixture.colors[static_cast<size_t>(channel) -
                                       static_cast<size_t>(PreviewChannel::D0)];
                if (channel == PreviewChannel::D0) {
                    for (size_t c = 0; c < 3; ++c)
                        color[c] *=
                            ((1 - u) * v * fixture.corner_colors[0][c] +
                             u * v * fixture.corner_colors[1][c] +
                             (1 - u) * (1 - v) * fixture.corner_colors[2][c] +
                             u * (1 - v) * fixture.corner_colors[3][c]) /
                            255;
                }
            } else if (texture) {
                const uint32_t tx = std::min(uint32_t(u * 24), 23U);
                const uint32_t ty = std::min(uint32_t(v * 16), 15U);
                const size_t offset =
                    ((ty / 8) * 3 + tx / 8) * kPreviewTextureFaceBytes +
                    ((ty % 8) * 8 + tx % 8) * 4;
                for (size_t c = 0; c < 3; ++c)
                    color[c] = texels[offset + c] / 255.0f;
            } else {
                const float scalar =
                    channel == PreviewChannel::DepthRamp ?
                        u :
                    channel == PreviewChannel::FixtureAlphaMask ?
                        float(alpha > fixture.alpha_reference / 255.0f) :
                        (fixture.profile == PreviewFixtureProfile::Fog ? u :
                                                                         1) *
                            fixture.fog;
                color[0] = color[1] = color[2] = scalar;
            }
            const size_t offset = (size_t(y) * width + x) * 4;
            for (size_t c = 0; c < 3; ++c)
                (*rgba)[offset + c] = byte(color[c]);
            (*rgba)[offset + 3] = 255;
        }
    }
    return true;
}
} // namespace xemu::shader_browser
