// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-model.hh"
#include "shader-browser-recipe-inspector.hh"
#include "shader-browser-details-model.hh"

namespace xemu::shader_browser {

// All fields are copied out of the browser and renderer before admission.
// No source vector, replacement payload, or fixture buffer is borrowed.
struct PreviewPacketInputs {
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
    bool animated = false;
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
