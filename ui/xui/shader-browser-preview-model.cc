// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-model.hh"

#include <algorithm>
#include <limits>

#include <xxhash.h>
#include <tuple>

namespace xemu::shader_browser {
namespace {

bool ContainsNul(const std::string &value)
{
    return value.find('\0') != std::string::npos;
}

bool DigestIsZero(const PreviewDigest &digest)
{
    for (uint8_t byte : digest) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

bool CheckedAdd(size_t lhs, size_t rhs, size_t *result)
{
    if (std::numeric_limits<size_t>::max() - lhs < rhs) {
        return false;
    }
    *result = lhs + rhs;
    return true;
}

} // namespace

bool PreviewSelection::operator==(const PreviewSelection &other) const
{
    return scope == other.scope && shader == other.shader &&
           session_epoch == other.session_epoch &&
           renderer_epoch == other.renderer_epoch &&
           backend == other.backend && mode == other.mode;
}

bool PreviewSelection::operator!=(const PreviewSelection &other) const
{
    return !(*this == other);
}

bool PreviewCompileKey::operator==(const PreviewCompileKey &other) const
{
    return selection == other.selection &&
           recipe_format_version == other.recipe_format_version &&
           generator_abi == other.generator_abi &&
           interface_abi == other.interface_abi &&
           replacement_id == other.replacement_id &&
           replacement_revision == other.replacement_revision &&
           source_digest == other.source_digest &&
           partner_digest == other.partner_digest;
}

bool PreviewCompileKey::operator!=(const PreviewCompileKey &other) const
{
    return !(*this == other);
}

bool PreviewResultKey::operator==(const PreviewResultKey &other) const
{
    return clock_revision == other.clock_revision &&
           clock_edit_revision == other.clock_edit_revision &&
           time_seconds == other.time_seconds && compile == other.compile &&
           input_revision == other.input_revision && scene == other.scene &&
           view_revision == other.view_revision && width == other.width &&
           height == other.height && packet_kind == other.packet_kind &&
           replay_class == other.replay_class &&
           fixture_digest == other.fixture_digest;
}

bool PreviewResultKey::operator!=(const PreviewResultKey &other) const
{
    return !(*this == other);
}

const char *PreviewModeLabel(PreviewMode mode)
{
    switch (mode) {
    case PreviewMode::Normal: return "Normal";
    case PreviewMode::Uber: return "Uber Shader";
    case PreviewMode::Replacement: return "Replacement Shader";
    case PreviewMode::Visualize: return "Visualize";
    }
    return "Unknown";
}

const char *PreviewBackendLabel(PreviewBackend backend)
{
    switch (backend) {
    case PreviewBackend::Unknown: return "Unknown";
    case PreviewBackend::OpenGL: return "OpenGL";
    case PreviewBackend::Vulkan: return "Vulkan";
    }
    return "Unknown";
}

const char *PreviewStateLabel(PreviewState state)
{
    switch (state) {
    case PreviewState::Disabled: return "Disabled";
    case PreviewState::Hidden: return "Hidden";
    case PreviewState::NoSelection: return "No selection";
    case PreviewState::WaitingForInputs: return "Waiting for inputs";
    case PreviewState::Debouncing: return "Debouncing";
    case PreviewState::NeedsPreparation: return "Needs preparation";
    case PreviewState::Preparing: return "Preparing";
    case PreviewState::Ready: return "Ready";
    case PreviewState::Rendering: return "Rendering";
    case PreviewState::Throttled: return "Throttled";
    case PreviewState::Frozen: return "Frozen";
    case PreviewState::Unsupported: return "Unsupported";
    case PreviewState::Failed: return "Failed";
    case PreviewState::Retiring: return "Retiring";
    }
    return "Unknown";
}

const char *PreviewPressureLabel(PreviewPressure pressure)
{
    switch (pressure) {
    case PreviewPressure::Normal: return "Normal";
    case PreviewPressure::Elevated: return "Elevated";
    case PreviewPressure::High: return "High";
    case PreviewPressure::Critical: return "Critical";
    }
    return "Unknown";
}

const char *PreviewReplayClassLabel(PreviewReplayClass replay_class)
{
    switch (replay_class) {
    case PreviewReplayClass::Synthetic: return "Synthetic";
    case PreviewReplayClass::Complete: return "Complete replay";
    case PreviewReplayClass::Approximate: return "Approximate replay";
    case PreviewReplayClass::Unsupported: return "Unsupported";
    }
    return "Unknown";
}

PreviewDigest ComputePreviewDigest(const uint8_t *data, size_t size)
{
    PreviewDigest digest{};
    if ((!data && size) || size == 0) {
        return digest;
    }
    XXH128_hash_t first = XXH3_128bits_withSeed(
        data, size, UINT64_C(0x58454d5550525631));
    XXH128_hash_t second = XXH3_128bits_withSeed(
        data, size, UINT64_C(0x58454d5550525632));
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, first);
    std::copy_n(canonical.digest, 16, digest.begin());
    XXH128_canonicalFromHash(&canonical, second);
    std::copy_n(canonical.digest, 16, digest.begin() + 16);
    return digest;
}

size_t PreviewPacketOwnedBytes(const PreviewPacket &packet, bool *overflow)
{
    size_t total = 0;
    bool valid = CheckedAdd(total, packet.recipe.capacity(), &total) &&
                 CheckedAdd(total, packet.source.capacity(), &total) &&
                 CheckedAdd(total, packet.partner_source.capacity(), &total) &&
                 CheckedAdd(total, packet.fixture_bytes.capacity(), &total);
    if (overflow) {
        *overflow = !valid;
    }
    return valid ? total : std::numeric_limits<size_t>::max();
}

bool ValidatePreviewPacket(const PreviewPacket &packet, std::string *error)
{
    auto fail = [error](const char *message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    // Reject retained allocations before digesting any caller supplied bytes.
    bool overflow = false;
    size_t bytes = PreviewPacketOwnedBytes(packet, &overflow);
    if (overflow || bytes > kPreviewMaxOwnedPacketBytes) {
        return fail("Preview packet exceeds the 32 MiB owned-data limit");
    }

    if (packet.selection.scope.title_id == 0) {
        return fail("Preview packet requires an explicit Xbox TitleID");
    }
    if (packet.selection.shader.stage != Stage::Pixel) {
        return fail("Stage 4 v1 supports pixel/fragment shaders only");
    }
    if (packet.selection.shader.hash.version == 0) {
        return fail("Preview shader identity version must be non-zero");
    }
    if (packet.selection.backend == PreviewBackend::Unknown) {
        return fail("Preview packet requires a resolved renderer backend");
    }
    if (packet.recipe_format_version == 0 || packet.recipe.empty() ||
        packet.recipe.size() > kPreviewMaxRecipeBytes) {
        return fail("Canonical recipe is missing or exceeds the 8192-byte limit");
    }
    ShaderHash recomputed = ComputeShaderHash(
        packet.selection.shader.hash.version, packet.selection.shader.stage,
        packet.recipe_format_version, packet.recipe.data(),
        packet.recipe.size());
    if (recomputed != packet.selection.shader.hash) {
        return fail("Canonical recipe does not match the selected shader hash");
    }
    if (packet.generator_abi == 0 || packet.interface_abi == 0) {
        return fail("Preview generator and interface ABI must be non-zero");
    }
    if (packet.source.size() > kPreviewMaxSourceBytes ||
        ContainsNul(packet.source)) {
        return fail("Preview source exceeds 4 MiB or contains embedded NUL");
    }
    if (!packet.source.empty()) {
        if (packet.source_digest != ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(packet.source.data()),
                packet.source.size())) {
            return fail("Preview source digest does not match its bytes");
        }
    } else if (!DigestIsZero(packet.source_digest)) {
        return fail("Preview source digest was supplied without source bytes");
    }
    if (packet.partner_source.size() > kPreviewMaxSourceBytes ||
        ContainsNul(packet.partner_source)) {
        return fail("Preview partner source exceeds 4 MiB or contains "
                    "embedded NUL");
    }
    if (!packet.partner_source.empty()) {
        if (packet.partner_digest != ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(packet.partner_source.data()),
                packet.partner_source.size())) {
            return fail("Preview partner digest does not match its bytes");
        }
    } else if (!DigestIsZero(packet.partner_digest)) {
        return fail("Preview partner digest was supplied without partner bytes");
    }
    if (!packet.fixture_bytes.empty()) {
        if (packet.fixture_digest != ComputePreviewDigest(
                packet.fixture_bytes.data(), packet.fixture_bytes.size())) {
            return fail("Preview fixture digest does not match its bytes");
        }
    } else if (!DigestIsZero(packet.fixture_digest)) {
        return fail("Preview fixture digest was supplied without fixture bytes");
    }
    if (packet.width == 0 || packet.height == 0 ||
        packet.width > kPreviewFullExtent ||
        packet.height > kPreviewFullExtent) {
        return fail("Preview extent must be between 1 and 320 pixels");
    }
    if (packet.packet_kind == PreviewPacketKind::Replay &&
        packet.replay_class == PreviewReplayClass::Unsupported) {
        return fail("Unsupported draw replay packets cannot be executed");
    }
    if (packet.packet_kind == PreviewPacketKind::Replay &&
        (packet.replay_class == PreviewReplayClass::Synthetic ||
         packet.fixture_bytes.empty())) {
        return fail("Replay packets require owned draw inputs and an exact or "
                    "approximate classification");
    }
    if (packet.packet_kind == PreviewPacketKind::Synthetic &&
        packet.replay_class != PreviewReplayClass::Synthetic) {
        return fail("Synthetic packets must use the synthetic classification");
    }
    if (packet.selection.mode == PreviewMode::Replacement) {
        if (packet.replacement_id == 0 ||
            packet.replacement_revision == 0 || packet.source.empty() ||
            DigestIsZero(packet.source_digest)) {
            return fail("Replacement preview requires immutable source identity");
        }
    }

    if (error) {
        error->clear();
    }
    return true;
}

PreviewCompileKey BuildPreviewCompileKey(const PreviewPacket &packet)
{
    PreviewCompileKey key{};
    key.selection = packet.selection;
    key.recipe_format_version = packet.recipe_format_version;
    key.generator_abi = packet.generator_abi;
    key.interface_abi = packet.interface_abi;
    key.replacement_id = packet.replacement_id;
    key.replacement_revision = packet.replacement_revision;
    key.source_digest = packet.source_digest;
    key.partner_digest = packet.partner_digest;
    return key;
}

PreviewResultKey BuildPreviewResultKey(const PreviewPacket &packet)
{
    PreviewResultKey key{};
    key.compile = BuildPreviewCompileKey(packet);
    key.input_revision = packet.input_revision;
    key.view_revision = packet.view_revision;
    key.scene = ClampPreviewScene(packet.scene);
    key.width = packet.width;
    key.height = packet.height;
    key.packet_kind = packet.packet_kind;
    key.replay_class = packet.replay_class;
    key.fixture_digest = packet.fixture_digest;
    return key;
}

} // namespace xemu::shader_browser
