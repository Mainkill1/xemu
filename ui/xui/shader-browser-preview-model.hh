// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"
#include "shader-browser-preview-scene.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xemu::shader_browser {

constexpr size_t kPreviewMaxRecipeBytes = 8192U;
constexpr size_t kPreviewMaxSourceBytes = 4U * 1024U * 1024U;
constexpr size_t kPreviewMaxOwnedPacketBytes = 32U * 1024U * 1024U;
constexpr uint32_t kPreviewFullExtent = 320U;
constexpr uint32_t kPreviewReducedExtent = 160U;
constexpr size_t kPreviewDigestBytes = 32U;

using PreviewDigest = std::array<uint8_t, kPreviewDigestBytes>;

enum class PreviewUpdatePolicy : uint8_t { OnDirty, Continuous };

enum class PreviewMode : uint8_t {
    Normal,
    Uber,
    Replacement,
    Visualize,
};

enum class PreviewBackend : uint8_t {
    Unknown,
    OpenGL,
    Vulkan,
};

enum class PreviewPacketKind : uint8_t {
    Synthetic,
    Replay,
};

enum class PreviewReplayClass : uint8_t {
    Synthetic,
    Complete,
    Approximate,
    Unsupported,
};

enum class PreviewState : uint8_t {
    Disabled,
    Hidden,
    NoSelection,
    WaitingForInputs,
    Debouncing,
    NeedsPreparation,
    Preparing,
    Ready,
    Rendering,
    Throttled,
    Frozen,
    Unsupported,
    Failed,
    Retiring,
};

enum class PreviewPressure : uint8_t {
    Normal,
    Elevated,
    High,
    Critical,
};

enum class PreviewWorkKind : uint8_t {
    None,
    Prepare,
    Render,
};

enum class PreviewSlotState : uint8_t {
    Free,
    Rendering,
    Ready,
    DisplayLeased,
    Retiring,
};

struct PreviewSelection {
    ShaderScope scope;
    ShaderKey shader;
    uint64_t session_epoch = 0;
    uint64_t renderer_epoch = 0;
    PreviewBackend backend = PreviewBackend::Unknown;
    PreviewMode mode = PreviewMode::Normal;

    bool operator==(const PreviewSelection &other) const;
    bool operator!=(const PreviewSelection &other) const;
};

struct PreviewCompileKey {
    PreviewSelection selection;
    uint32_t recipe_format_version = 0;
    uint32_t generator_abi = 0;
    uint32_t interface_abi = 0;
    uint64_t replacement_id = 0;
    uint64_t replacement_revision = 0;
    PreviewDigest source_digest{};
    PreviewDigest partner_digest{};

    bool operator==(const PreviewCompileKey &other) const;
    bool operator!=(const PreviewCompileKey &other) const;
};

struct PreviewResultKey {
    PreviewScene scene;
    uint64_t clock_revision = 0;
    uint64_t clock_edit_revision = 0;
    double time_seconds = 0.0;
    PreviewCompileKey compile;
    uint64_t input_revision = 0;
    uint64_t view_revision = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    PreviewPacketKind packet_kind = PreviewPacketKind::Synthetic;
    PreviewReplayClass replay_class = PreviewReplayClass::Synthetic;
    PreviewDigest fixture_digest{};

    bool operator==(const PreviewResultKey &other) const;
    bool operator!=(const PreviewResultKey &other) const;
};

struct PreviewPacket {
    PreviewScene scene;
    PreviewSelection selection;
    uint32_t recipe_format_version = 0;
    std::vector<uint8_t> recipe;
    std::string source;
    std::string partner_source;
    std::vector<uint8_t> fixture_bytes;
    uint32_t generator_abi = 0;
    uint32_t interface_abi = 0;
    uint64_t replacement_id = 0;
    uint64_t replacement_revision = 0;
    PreviewDigest source_digest{};
    PreviewDigest partner_digest{};
    PreviewDigest fixture_digest{};
    uint64_t input_revision = 0;
    uint64_t view_revision = 0;
    uint32_t width = kPreviewFullExtent;
    uint32_t height = kPreviewFullExtent;
    PreviewUpdatePolicy update_policy = PreviewUpdatePolicy::OnDirty;
    PreviewPacketKind packet_kind = PreviewPacketKind::Synthetic;
    PreviewReplayClass replay_class = PreviewReplayClass::Synthetic;
};

const char *PreviewModeLabel(PreviewMode mode);
const char *PreviewBackendLabel(PreviewBackend backend);
const char *PreviewStateLabel(PreviewState state);
const char *PreviewPressureLabel(PreviewPressure pressure);
const char *PreviewReplayClassLabel(PreviewReplayClass replay_class);

PreviewDigest ComputePreviewDigest(const uint8_t *data, size_t size);
size_t PreviewPacketOwnedBytes(const PreviewPacket &packet, bool *overflow);
bool ValidatePreviewPacket(const PreviewPacket &packet, std::string *error);
PreviewCompileKey BuildPreviewCompileKey(const PreviewPacket &packet);
PreviewResultKey BuildPreviewResultKey(const PreviewPacket &packet);

} // namespace xemu::shader_browser
