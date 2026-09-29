// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader-browser-capture-session.hh"

namespace xemu::asset_browser {
namespace capture = xemu::shader_browser;

struct AssetLimits {
    size_t maximum_events = 32768, maximum_parts = 2048;
    size_t maximum_vertices = 1048576, maximum_indices = 3145728;
    uint64_t decoded_byte_budget = 128U * 1024U * 1024U;
};
struct AssetVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
    std::array<float, 4> color{ 1, 1, 1, 1 };
};
enum class AssetStatus : uint8_t {
    Ready,
    Missing,
    Unsupported,
    Malformed,
    Pending,
    BudgetExceeded
};
using AssetMatrix = std::array<float, 16>; // Row major; column vectors.
struct AssetPlacement {
    AssetMatrix clip_from_local{};
    bool valid = false;
    std::string reason;
};
struct AssetPart {
    uint64_t id = 0, frame = 0;
    std::shared_ptr<const capture::CaptureOccurrence> occurrence;
    std::vector<AssetVertex> vertices;
    std::vector<uint32_t> source_vertices;
    std::vector<uint32_t> indices;
    capture::Bounds3 bounds;
    AssetPlacement placement;
    capture::CaptureDigest geometry_signature{};
    AssetStatus status = AssetStatus::Missing;
    bool has_uv = false, has_normals = false, has_color = false;
    uint64_t decoded_bytes = 0;
    std::string reason;
};
using SharedAssetPart = std::shared_ptr<const AssetPart>;
struct AssetAssembly {
    uint64_t id = 0, frame = 0;
    capture::CaptureSessionContext context;
    std::vector<SharedAssetPart> parts;
    std::vector<AssetMatrix> anchor_from_local;
    AssetMatrix local_from_captured_clip{};
    bool captured_placement = false;
    capture::Bounds3 bounds;
    std::string label;
    bool user_confirmed = false;
    bool complete = false;
};
struct AssetCatalog {
    std::shared_ptr<const capture::CaptureSessionSnapshot> recording;
    capture::CaptureSessionContext context;
    uint64_t frame = 0, decoded_bytes = 0, matching_draws = 0;
    std::vector<SharedAssetPart> parts;
    std::vector<AssetAssembly> entries;
    bool complete_frame = false, budget_exceeded = false;
    std::string reason;
};
AssetCatalog BuildAssetCatalog(const capture::CaptureSessionSnapshot &,
                               const AssetLimits & = {});
AssetAssembly MakeAssetAssembly(const AssetCatalog &,
                                const std::vector<uint64_t> &,
                                const std::string &label,
                                bool confirmed = true);
const char *AssetStatusLabel(AssetStatus);
} // namespace xemu::asset_browser
