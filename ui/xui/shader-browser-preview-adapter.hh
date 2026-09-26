// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-model.hh"
#include "shader-browser-recipe-inspector.hh"
#include "shader-browser-details-model.hh"

namespace xemu::shader_browser {

constexpr size_t kPreviewSyntheticFixtureBytes = 83U;

struct PreviewSyntheticFixture {
    std::array<std::array<uint8_t, 4>, 4> corner_colors{};
    std::array<std::array<uint8_t, 4>, 4> texture_texels{};
    std::array<float, 2> uv_scale{1.0f, 1.0f};
    std::array<float, 2> uv_offset{0.0f, 0.0f};
    std::array<float, 4> constant_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> fog_color{0.0f, 0.0f, 0.0f, 0.0f};
    uint8_t alpha_reference = 0;
    uint8_t linear_filter = 1;
    uint8_t repeat_wrap = 0;
};

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
