// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-model.hh"
#include "shader-browser-recipe-inspector.hh"
#include "shader-browser-details-model.hh"

namespace xemu::shader_browser {

enum class PreviewFixtureProfile : uint8_t {
    Flat,
    UV,
    Checker,
    Alpha,
    MultiTexture,
    Normal,
    Cubemap,
    Fog,
    Diagnostic
};
constexpr size_t kPreviewTextureExtent = 8;
constexpr size_t kPreviewTextureFaceBytes = 8 * 8 * 4;
constexpr size_t kPreviewFixtureTextureBytes = 4 * 6 * kPreviewTextureFaceBytes;
constexpr size_t kPreviewSyntheticFixtureBytes = 83 + 4 + 1 + 4 + 64 + 4 + 12;
const char *PreviewFixtureProfileName(PreviewFixtureProfile profile);

struct PreviewSyntheticFixture {
    std::array<std::array<uint8_t, 4>, 4> corner_colors{};
    std::array<std::array<uint8_t, 4>, 4> texture_texels{};
    std::array<float, 2> uv_scale{1.0f, 1.0f};
    std::array<float, 2> uv_offset{0.0f, 0.0f};
    std::array<float, 4> constant_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> fog_color{0.0f, 0.0f, 0.0f, 0.0f};
    PreviewFixtureProfile profile = PreviewFixtureProfile::Flat;
    std::array<PreviewFixtureProfile, 4> textures{};
    std::array<std::array<float, 4>, 4> colors{
        { { 1, 1, 1, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 0, 1 } }
    };
    float fog = 0;
    std::array<float, 3> cube_direction{ 0, 0, 1 };
    uint8_t alpha_reference = 0;
    uint8_t linear_filter = 1;
    uint8_t repeat_wrap = 0;
};

PreviewSyntheticFixture MakePreviewFixture(PreviewFixtureProfile profile);
bool SuggestPreviewFixture(const CanonicalRecipe &recipe,
                           PreviewFixtureProfile *profile, std::string *error);
using PreviewTexturePixels = std::array<uint8_t, 6 * kPreviewTextureFaceBytes>;
PreviewTexturePixels
GeneratePreviewTexture(const PreviewSyntheticFixture &fixture, size_t stage);

// Bounded bottom-up RGBA8 charts; texture diagnostics show a 3x2 face atlas.
bool RenderPreviewDiagnostic(PreviewChannel channel,
                             const PreviewSyntheticFixture &fixture,
                             uint32_t width, uint32_t height,
                             std::vector<uint8_t> *rgba, std::string *error);
void ApplyPreviewOutputChannel(PreviewChannel channel,
                               std::vector<uint8_t> *rgba);

// Apply fixture values once, using the untransformed mesh UVs for corner colors.
void ApplyPreviewSyntheticFixture(const PreviewSyntheticFixture &fixture,
                                  std::vector<PreviewSceneVertex> &vertices,
                                  const std::array<bool, 4> &cube_stages = {});

void AnimatePreviewSyntheticFixture(PreviewSyntheticFixture *fixture,
                                    double time_seconds);

std::vector<uint8_t> EncodePreviewSyntheticFixture(
    const PreviewSyntheticFixture &fixture);
bool DecodePreviewSyntheticFixture(const std::vector<uint8_t> &bytes,
                                   PreviewSyntheticFixture *fixture,
                                   std::string *error);

// All fields are copied out of the browser and renderer before admission.
// No source vector, replacement payload, or fixture buffer is borrowed.
struct PreviewPacketInputs {
    PreviewScene scene;
    PreviewSelection selection;
    CanonicalRecipe recipe;
    std::string source;
    std::string partner_source;
    std::vector<uint8_t> fixture_bytes;
    uint32_t generator_abi = 0;
    uint32_t interface_abi = 0;
    uint64_t replacement_id = 0;
    uint64_t replacement_revision = 0;
    uint64_t input_revision = 0;
    uint64_t view_revision = 0;
    uint32_t width = kPreviewFullExtent;
    uint32_t height = kPreviewFullExtent;
    PreviewUpdatePolicy update_policy = PreviewUpdatePolicy::OnDirty;
};

// A live snapshot can gain another build scope after the selected recipe was
// copied. Bind only a scope observed for the same shader in the current entry.
bool AttachPreviewSelectionScope(const Entry &entry,
                                 const PreviewSelection &selection,
                                 CanonicalRecipe *recipe, std::string *error);

bool BuildPreviewPacket(const PreviewPacketInputs &inputs,
                        PreviewPacket *packet, std::string *error);

// Copies only the selected resident fragment source. The detail service has
// already detached these bytes from renderer-owned storage.
bool CopyPreviewFragmentSource(const PreviewSelection &selection,
                               const DetailSnapshot &detail,
                               std::string *source, std::string *error);

// A deterministic private partner stage for synthetic inputs. A missing or
// incompatible interface is rejected later by private program preparation.
std::string BuildPreviewSyntheticVertexSource(
    const std::string &fragment_source, PreviewBackend backend);

} // namespace xemu::shader_browser
