// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace xemu::shader_browser {
enum class PreviewMesh { Quad, Sphere, Cube };
enum class PreviewReferenceId : uint8_t {
    Backdrop,
    Ground,
    Intersection,
    Blocker,
    Count
};
constexpr size_t kPreviewReferenceCount =
    static_cast<size_t>(PreviewReferenceId::Count);
struct PreviewReference {
    bool visible = true;
    std::array<float, 3> translation{};
    bool operator==(const PreviewReference &other) const;
};
struct PreviewScene {
    uint32_t version = 1;
    PreviewMesh mesh = PreviewMesh::Quad;
    float yaw = 0, pitch = 0, distance = 4;
    std::array<float, 2> pan{};
    std::array<float, 3> target_pivot{};
    std::array<PreviewReference, kPreviewReferenceCount> references{};
    bool operator==(const PreviewScene &other) const;
};
enum class PreviewBlendMode : uint8_t { Opaque, Alpha };
enum class PreviewCullMode : uint8_t { None, Back, Front };
struct PreviewRenderState {
    PreviewBlendMode blend = PreviewBlendMode::Opaque;
    bool depth_test = true;
    bool depth_write = true;
    PreviewCullMode cull = PreviewCullMode::None;
    bool alpha_test = false;
    uint8_t alpha_reference = 0;
    std::array<float, 4> clear_color{ 0.08f, 0.08f, 0.08f, 1.0f };
    bool operator==(const PreviewRenderState &other) const;
};
constexpr size_t kPreviewMaxSceneVertices = 4096;
constexpr size_t kPreviewMaxSceneDraws = kPreviewReferenceCount + 1;
struct PreviewSceneVertex {
    float position[4];
    float color[4];
    float uv[2];
    float colors[4][4];
    float fog;
    float direction[3];
    float cube_stages[4];
};
enum class PreviewSceneRole : uint8_t {
    Backdrop,
    Ground,
    Intersection,
    Blocker,
    Target
};
struct PreviewSceneDraw {
    PreviewSceneRole role = PreviewSceneRole::Target;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
};
struct PreviewSceneFrame {
    std::vector<PreviewSceneVertex> vertices;
    std::array<PreviewSceneDraw, kPreviewMaxSceneDraws> draws{};
    size_t draw_count = 0;
};
// Raw float positions from one submitted game draw. Indices retain the exact
// triangle order; the private preview supplies synthetic fragment inputs.
struct PreviewCapturedMesh {
    std::vector<std::array<float, 4>> positions;
    std::vector<uint32_t> indices;
};
enum class PreviewCameraGestureKind : uint8_t {
    Orbit,
    Pan,
    Dolly,
    FocusTarget,
    ResetCamera,
    ResetScene
};
struct PreviewCameraGesture {
    PreviewCameraGestureKind kind = PreviewCameraGestureKind::Orbit;
    // Drag deltas are normalized by viewport width/height; dolly uses wheel steps.
    float x = 0;
    float y = 0;
};
PreviewScene ClampPreviewScene(PreviewScene scene);
PreviewRenderState ClampPreviewRenderState(PreviewRenderState state);
PreviewScene ApplyPreviewCameraGesture(PreviewScene scene,
                                       PreviewCameraGesture gesture);
PreviewSceneFrame BuildPreviewSceneFrame(const PreviewScene &scene,
                                         float aspect = 1.0f,
                                         bool vulkan = false);
PreviewSceneFrame BuildPreviewCapturedFrame(const PreviewScene &scene,
                                             const PreviewCapturedMesh &mesh,
                                             float aspect = 1.0f,
                                             bool vulkan = false);
std::vector<PreviewSceneVertex>
BuildPreviewSceneGeometry(const PreviewScene &scene, float aspect = 1.0f,
                          bool vulkan = false);
} // namespace xemu::shader_browser
