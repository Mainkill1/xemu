// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <vector>
namespace xemu::shader_browser {
enum class PreviewMesh { Quad, Sphere, Cube };
struct PreviewScene {
    PreviewMesh mesh = PreviewMesh::Quad;
    float yaw = 0, pitch = 0, distance = 4;
    std::array<float, 2> pan{};
    bool operator==(const PreviewScene &other) const;
};
constexpr size_t kPreviewMaxSceneVertices = 32 * 16 * 6;
struct PreviewSceneVertex {
    float position[4];
    float color[4];
    float uv[2];
    float colors[4][4];
    float fog;
    float direction[3];
    float cube_stages[4];
};
PreviewScene ClampPreviewScene(PreviewScene scene);
std::vector<PreviewSceneVertex>
BuildPreviewSceneGeometry(const PreviewScene &scene, float aspect = 1.0f,
                          bool vulkan = false);
} // namespace xemu::shader_browser
