// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-adapter.hh"

#include <algorithm>
#include <utility>

namespace xemu::shader_browser {

bool BuildPreviewPacket(const PreviewPacketInputs &inputs,
                        PreviewPacket *packet, std::string *error)
{
    auto fail = [error](const char *message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (!packet) {
        return fail("Preview packet destination is null");
    }
    if (inputs.recipe.key != inputs.selection.shader) {
        return fail("Canonical recipe is not the selected shader");
    }
    if (!inputs.recipe.scopes.empty() &&
        std::find(inputs.recipe.scopes.begin(), inputs.recipe.scopes.end(),
                  inputs.selection.scope) == inputs.recipe.scopes.end()) {
        return fail("Canonical recipe has no selected title/build scope");
    }
    if (inputs.fixture_bytes.empty()) {
        return fail("Synthetic preview requires owned fixture bytes");
    }
    size_t remaining = kPreviewMaxOwnedPacketBytes;
    auto charge = [&remaining](size_t capacity) {
        if (capacity > remaining) {
            return false;
        }
        remaining -= capacity;
        return true;
    };
    if (!charge(inputs.recipe.bytes.capacity()) ||
        !charge(inputs.source.capacity()) ||
        !charge(inputs.partner_source.capacity()) ||
        !charge(inputs.fixture_bytes.capacity())) {
        return fail("Preview packet exceeds the 32 MiB retained-data limit");
    }
    if (inputs.source.size() > kPreviewMaxSourceBytes ||
        inputs.partner_source.size() > kPreviewMaxSourceBytes ||
        inputs.recipe.bytes.size() > kPreviewMaxRecipeBytes) {
        return fail("Preview source or recipe exceeds its size limit");
    }

    PreviewPacket candidate{};
    candidate.selection = inputs.selection;
    candidate.recipe_format_version = inputs.recipe.recipe_format_version;
    candidate.recipe = inputs.recipe.bytes;
    candidate.source = inputs.source;
    candidate.partner_source = inputs.partner_source;
    candidate.fixture_bytes = inputs.fixture_bytes;
    candidate.generator_abi = inputs.generator_abi;
    candidate.interface_abi = inputs.interface_abi;
    candidate.replacement_id = inputs.replacement_id;
    candidate.replacement_revision = inputs.replacement_revision;
    candidate.input_revision = inputs.input_revision;
    candidate.view_revision = inputs.view_revision;
    candidate.width = inputs.width;
    candidate.height = inputs.height;
    candidate.animated = inputs.animated;
    candidate.packet_kind = PreviewPacketKind::Synthetic;
    candidate.replay_class = PreviewReplayClass::Synthetic;
    if (!candidate.source.empty()) {
        candidate.source_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(candidate.source.data()),
            candidate.source.size());
    }
    if (!candidate.partner_source.empty()) {
        candidate.partner_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(candidate.partner_source.data()),
            candidate.partner_source.size());
    }
    candidate.fixture_digest = ComputePreviewDigest(
        candidate.fixture_bytes.data(), candidate.fixture_bytes.size());
    if (!ValidatePreviewPacket(candidate, error)) {
        return false;
    }
    *packet = std::move(candidate);
    return true;
}

} // namespace xemu::shader_browser
