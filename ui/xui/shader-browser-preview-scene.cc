// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-scene.hh"
#include <algorithm>
#include <cmath>
namespace xemu::shader_browser {
bool PreviewScene::operator==(const PreviewScene &o) const
{
    return version == o.version && mesh == o.mesh && yaw == o.yaw &&
           pitch == o.pitch && distance == o.distance && pan == o.pan &&
           target_pivot == o.target_pivot && references == o.references;
}
bool PreviewReference::operator==(const PreviewReference &o) const
{
    return visible == o.visible && translation == o.translation;
}
bool PreviewRenderState::operator==(const PreviewRenderState &o) const
{
    return blend == o.blend && depth_test == o.depth_test &&
           depth_write == o.depth_write && cull == o.cull &&
           alpha_test == o.alpha_test &&
           alpha_reference == o.alpha_reference &&
           clear_color == o.clear_color;
}
PreviewScene ClampPreviewScene(PreviewScene s)
{
    auto bound = [](float v, float lo, float hi, float fallback) {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    s.version = 1;
    if (s.mesh != PreviewMesh::Quad && s.mesh != PreviewMesh::Sphere &&
        s.mesh != PreviewMesh::Cube)
        s.mesh = PreviewMesh::Quad;
    s.yaw = bound(s.yaw, -180, 180, 0);
    s.pitch = bound(s.pitch, -85, 85, 0);
    s.distance = bound(s.distance, 2.5f, 12, 4);
    for (auto &p : s.pan)
        p = bound(p, -2, 2, 0);
    for (auto &p : s.target_pivot)
        p = bound(p, -1.5f, 1.5f, 0);
    for (auto &reference : s.references)
        for (auto &p : reference.translation)
            p = bound(p, -0.75f, 0.75f, 0);
    return s;
}
PreviewRenderState ClampPreviewRenderState(PreviewRenderState state)
{
    if (state.blend != PreviewBlendMode::Opaque &&
        state.blend != PreviewBlendMode::Alpha)
        state.blend = PreviewBlendMode::Opaque;
    if (state.cull != PreviewCullMode::None &&
        state.cull != PreviewCullMode::Back &&
        state.cull != PreviewCullMode::Front)
        state.cull = PreviewCullMode::None;
    const float fallback[] = { 0.08f, 0.08f, 0.08f, 1.0f };
    for (size_t i = 0; i < state.clear_color.size(); ++i)
        state.clear_color[i] = std::isfinite(state.clear_color[i]) ?
            std::clamp(state.clear_color[i], 0.0f, 1.0f) : fallback[i];
    return state;
}
PreviewScene ApplyPreviewCameraGesture(PreviewScene scene,
                                       PreviewCameraGesture gesture)
{
    scene = ClampPreviewScene(scene);
    if (!std::isfinite(gesture.x) || !std::isfinite(gesture.y))
        return scene;
    switch (gesture.kind) {
    case PreviewCameraGestureKind::Orbit:
        scene.yaw += 180.0f * gesture.x;
        scene.pitch -= 180.0f * gesture.y;
        break;
    case PreviewCameraGestureKind::Pan:
        scene.pan[0] += gesture.x * scene.distance * 0.8f;
        scene.pan[1] -= gesture.y * scene.distance * 0.8f;
        break;
    case PreviewCameraGestureKind::Dolly:
        scene.distance *= std::pow(0.85f, gesture.y);
        break;
    case PreviewCameraGestureKind::FocusTarget:
        scene.pan = {};
        scene.target_pivot = {};
        break;
    case PreviewCameraGestureKind::ResetCamera:
        scene.yaw = scene.pitch = 0;
        scene.distance = 4;
        scene.pan = {};
        scene.target_pivot = {};
        break;
    case PreviewCameraGestureKind::ResetScene:
        return {};
    }
    return ClampPreviewScene(scene);
}
std::vector<PreviewSceneVertex>
BuildPreviewSceneGeometry(const PreviewScene &input, float aspect, bool vulkan)
{
    const auto s = ClampPreviewScene(input);
    constexpr float pi = 3.14159265358979323846f;
    const float cy = std::cos(s.yaw * pi / 180),
                sy = std::sin(s.yaw * pi / 180);
    const float cp = std::cos(s.pitch * pi / 180),
                sp = std::sin(s.pitch * pi / 180);
    aspect = std::isfinite(aspect) ? std::clamp(aspect, 0.25f, 4.0f) : 1;
    struct Point {
        float x, y, z, u, v;
    };
    std::vector<PreviewSceneVertex> out;
    out.reserve(kPreviewMaxSceneVertices);
    auto triangle = [&](Point a, Point b, Point c) {
        Point points[] = { a, b, c };
        for (auto &p : points) {
            float x = cy * p.x + sy * p.z, z = -sy * p.x + cy * p.z;
            p.x = x + s.pan[0];
            p.z = sp * p.y + cp * z - s.distance;
            p.y = cp * p.y - sp * z + s.pan[1];
        }
        a = points[0];
        b = points[1];
        c = points[2];
        float nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
        float ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        float nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        // Closed convex surfaces need only their camera-facing triangles.
        // The two-sided quad reverses winding when viewed from behind.
        float facing = -(nx * a.x + ny * a.y + nz * a.z);
        if (std::abs(facing) < 1e-7f)
            return;
        if (facing < 0) {
            if (s.mesh != PreviewMesh::Quad)
                return;
            std::swap(points[1], points[2]);
        }
        for (const auto &p : points) {
            PreviewSceneVertex v{};
            const float near = 0.1f, far = 32;
            v.position[0] = 2.41421356f * p.x / aspect;
            v.position[1] = 2.41421356f * p.y * (vulkan ? -1 : 1);
            v.position[2] = -(far + near) / (far - near) * p.z -
                            2 * far * near / (far - near);
            v.position[3] = -p.z;
            if (vulkan)
                v.position[2] = (v.position[2] + v.position[3]) / 2;
            v.uv[0] = p.u;
            v.uv[1] = p.v;
            v.color[0] = p.u;
            v.color[1] = p.v;
            v.color[2] = 1 - p.u;
            v.color[3] = 1;
            out.push_back(v);
        }
    };
    auto face = [&](Point a, Point b, Point c, Point d) {
        triangle(a, b, c);
        triangle(a, c, d);
    };
    if (s.mesh == PreviewMesh::Quad) {
        face({ -1, -1, 0, 0, 0 }, { 1, -1, 0, 1, 0 }, { 1, 1, 0, 1, 1 },
             { -1, 1, 0, 0, 1 });
    } else if (s.mesh == PreviewMesh::Cube) {
        // Face-local UVs, outward counter-clockwise winding.
        face({ -1, -1, 1, 0, 0 }, { 1, -1, 1, 1, 0 }, { 1, 1, 1, 1, 1 },
             { -1, 1, 1, 0, 1 });
        face({ 1, -1, -1, 0, 0 }, { -1, -1, -1, 1, 0 }, { -1, 1, -1, 1, 1 },
             { 1, 1, -1, 0, 1 });
        face({ 1, -1, 1, 0, 0 }, { 1, -1, -1, 1, 0 }, { 1, 1, -1, 1, 1 },
             { 1, 1, 1, 0, 1 });
        face({ -1, -1, -1, 0, 0 }, { -1, -1, 1, 1, 0 }, { -1, 1, 1, 1, 1 },
             { -1, 1, -1, 0, 1 });
        face({ -1, 1, 1, 0, 0 }, { 1, 1, 1, 1, 0 }, { 1, 1, -1, 1, 1 },
             { -1, 1, -1, 0, 1 });
        face({ -1, -1, -1, 0, 0 }, { 1, -1, -1, 1, 0 }, { 1, -1, 1, 1, 1 },
             { -1, -1, 1, 0, 1 });
    } else {
        auto sphere = [&](int x, int y) {
            float u = float(x) / 32, v = float(y) / 16;
            float radius = std::sin(pi * v);
            return Point{ radius * std::cos(2 * pi * u), -std::cos(pi * v),
                          radius * std::sin(2 * pi * u), u, v };
        };
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 32; ++x)
                face(sphere(x, y), sphere(x, y + 1), sphere(x + 1, y + 1),
                     sphere(x + 1, y));
    }
    return out;
}
PreviewSceneFrame BuildPreviewSceneFrame(const PreviewScene &input,
                                        float aspect, bool vulkan)
{
    const PreviewScene scene = ClampPreviewScene(input);
    PreviewSceneFrame frame;
    frame.vertices.reserve(kPreviewMaxSceneVertices);
    aspect = std::isfinite(aspect) ? std::clamp(aspect, 0.25f, 4.0f) : 1;
    constexpr float pi = 3.14159265358979323846f;
    const float cy = std::cos(scene.yaw * pi / 180),
                sy = std::sin(scene.yaw * pi / 180);
    const float cp = std::cos(scene.pitch * pi / 180),
                sp = std::sin(scene.pitch * pi / 180);
    struct Point { float x, y, z, u, v; };
    auto vertex = [&](Point p, const std::array<float, 3> &translation,
                      const std::array<float, 4> &color, bool target) {
        p.x += translation[0] - scene.target_pivot[0];
        p.y += translation[1] - scene.target_pivot[1];
        p.z += translation[2] - scene.target_pivot[2];
        const float x = cy * p.x + sy * p.z;
        const float z = -sy * p.x + cy * p.z;
        p.x = x + scene.pan[0];
        p.z = sp * p.y + cp * z - scene.distance;
        p.y = cp * p.y - sp * z + scene.pan[1];
        PreviewSceneVertex v{};
        constexpr float near = 0.1f, far = 32;
        v.position[0] = 2.41421356f * p.x / aspect;
        v.position[1] = 2.41421356f * p.y * (vulkan ? -1 : 1);
        v.position[2] = -(far + near) / (far - near) * p.z -
                        2 * far * near / (far - near);
        v.position[3] = -p.z;
        if (vulkan) v.position[2] = (v.position[2] + v.position[3]) / 2;
        v.uv[0] = p.u;
        v.uv[1] = p.v;
        for (size_t i = 0; i < 4; ++i) v.color[i] = color[i];
        if (target) {
            v.color[0] = p.u;
            v.color[1] = p.v;
            v.color[2] = 1 - p.u;
            v.color[3] = 1;
        }
        return v;
    };
    auto face = [&](Point a, Point b, Point c, Point d,
                    const std::array<float, 3> &translation,
                    const std::array<float, 4> &color,
                    bool target = false) {
        if (frame.vertices.size() + 6 > kPreviewMaxSceneVertices) return;
        for (Point p : { a, b, c, a, c, d })
            frame.vertices.push_back(vertex(p, translation, color, target));
    };
    auto begin = [&](PreviewSceneRole role) {
        auto &draw = frame.draws[frame.draw_count++];
        draw.role = role;
        draw.first_vertex = static_cast<uint32_t>(frame.vertices.size());
    };
    auto end = [&] {
        auto &draw = frame.draws[frame.draw_count - 1];
        draw.vertex_count = static_cast<uint32_t>(frame.vertices.size()) -
                            draw.first_vertex;
    };
    for (size_t i = 0; i < kPreviewReferenceCount; ++i) {
        const PreviewReference &reference = scene.references[i];
        begin(static_cast<PreviewSceneRole>(i));
        if (reference.visible) {
            const auto &t = reference.translation;
            if (i == static_cast<size_t>(PreviewReferenceId::Backdrop)) {
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        const float x0 = -3.2f + 1.6f * x;
                        const float y0 = -3.2f + 1.6f * y;
                        const std::array<float, 4> color = (x + y) & 1 ?
                            std::array<float, 4>{ 0.16f, 0.27f, 0.37f, 1 } :
                            std::array<float, 4>{ 0.23f, 0.37f, 0.44f, 1 };
                        face({ x0, y0, -3, 0, 0 },
                             { x0 + 1.6f, y0, -3, 1, 0 },
                             { x0 + 1.6f, y0 + 1.6f, -3, 1, 1 },
                             { x0, y0 + 1.6f, -3, 0, 1 }, t, color);
                    }
            } else if (i == static_cast<size_t>(PreviewReferenceId::Ground)) {
                for (int z = 0; z < 4; ++z)
                    for (int x = 0; x < 4; ++x) {
                        const float x0 = -3.0f + 1.5f * x;
                        const float z0 = -2.7f + 1.35f * z;
                        const std::array<float, 4> color = (x + z) & 1 ?
                            std::array<float, 4>{ 0.27f, 0.29f, 0.31f, 1 } :
                            std::array<float, 4>{ 0.39f, 0.41f, 0.43f, 1 };
                        face({ x0, -1.35f, z0, 0, 0 },
                             { x0, -1.35f, z0 + 1.35f, 0, 1 },
                             { x0 + 1.5f, -1.35f, z0 + 1.35f, 1, 1 },
                             { x0 + 1.5f, -1.35f, z0, 1, 0 }, t, color);
                    }
            } else if (i == static_cast<size_t>(PreviewReferenceId::Intersection)) {
                const std::array<float, 4> color{ 0.95f, 0.72f, 0.19f, 1 };
                const float x0 = 0.4f, x1 = 1.1f, y0 = -0.65f,
                            y1 = 0.65f, z0 = -0.35f, z1 = 0.65f;
                face({ x0,y0,z1,0,0 }, { x1,y0,z1,1,0 },
                     { x1,y1,z1,1,1 }, { x0,y1,z1,0,1 }, t, color);
                face({ x1,y0,z0,0,0 }, { x0,y0,z0,1,0 },
                     { x0,y1,z0,1,1 }, { x1,y1,z0,0,1 }, t, color);
                face({ x1,y0,z1,0,0 }, { x1,y0,z0,1,0 },
                     { x1,y1,z0,1,1 }, { x1,y1,z1,0,1 }, t, color);
                face({ x0,y0,z0,0,0 }, { x0,y0,z1,1,0 },
                     { x0,y1,z1,1,1 }, { x0,y1,z0,0,1 }, t, color);
                face({ x0,y1,z1,0,0 }, { x1,y1,z1,1,0 },
                     { x1,y1,z0,1,1 }, { x0,y1,z0,0,1 }, t, color);
                face({ x0,y0,z0,0,0 }, { x1,y0,z0,1,0 },
                     { x1,y0,z1,1,1 }, { x0,y0,z1,0,1 }, t, color);
            } else {
                face({ -0.9f,-0.8f,1.35f,0,0 },
                     { -0.35f,-0.8f,1.35f,1,0 },
                     { -0.35f,0.8f,1.35f,1,1 },
                     { -0.9f,0.8f,1.35f,0,1 }, t,
                     { 0.16f, 0.82f, 0.84f, 1 });
            }
        }
        end();
    }
    begin(PreviewSceneRole::Target);
    constexpr std::array<float, 3> origin{};
    constexpr std::array<float, 4> white{ 1, 1, 1, 1 };
    if (scene.mesh == PreviewMesh::Quad) {
        face({ -1,-1,0,0,0 }, { 1,-1,0,1,0 },
             { 1,1,0,1,1 }, { -1,1,0,0,1 }, origin, white, true);
    } else if (scene.mesh == PreviewMesh::Cube) {
        const float x0 = -1, x1 = 1, y0 = -1, y1 = 1, z0 = -1, z1 = 1;
        face({ x0,y0,z1,0,0 }, { x1,y0,z1,1,0 },
             { x1,y1,z1,1,1 }, { x0,y1,z1,0,1 }, origin, white, true);
        face({ x1,y0,z0,0,0 }, { x0,y0,z0,1,0 },
             { x0,y1,z0,1,1 }, { x1,y1,z0,0,1 }, origin, white, true);
        face({ x1,y0,z1,0,0 }, { x1,y0,z0,1,0 },
             { x1,y1,z0,1,1 }, { x1,y1,z1,0,1 }, origin, white, true);
        face({ x0,y0,z0,0,0 }, { x0,y0,z1,1,0 },
             { x0,y1,z1,1,1 }, { x0,y1,z0,0,1 }, origin, white, true);
        face({ x0,y1,z1,0,0 }, { x1,y1,z1,1,0 },
             { x1,y1,z0,1,1 }, { x0,y1,z0,0,1 }, origin, white, true);
        face({ x0,y0,z0,0,0 }, { x1,y0,z0,1,0 },
             { x1,y0,z1,1,1 }, { x0,y0,z1,0,1 }, origin, white, true);
    } else {
        auto sphere = [](int x, int y) {
            const float u = float(x) / 32, v = float(y) / 16;
            const float radius = std::sin(pi * v);
            return Point{ radius * std::cos(2 * pi * u),
                          -std::cos(pi * v),
                          radius * std::sin(2 * pi * u), u, v };
        };
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 32; ++x)
                face(sphere(x, y), sphere(x, y + 1),
                     sphere(x + 1, y + 1), sphere(x + 1, y),
                     origin, white, true);
    }
    end();
    return frame;
}

PreviewSceneFrame BuildPreviewCapturedFrame(const PreviewScene &input,
                                            const PreviewCapturedMesh &mesh,
                                            float aspect, bool vulkan)
{
    PreviewSceneFrame frame = BuildPreviewSceneFrame(input, aspect, vulkan);
    if (frame.draw_count != kPreviewMaxSceneDraws) return {};
    PreviewSceneDraw &target = frame.draws[frame.draw_count - 1];
    frame.vertices.resize(target.first_vertex);
    target.vertex_count = 0;
    if (mesh.positions.empty() || mesh.indices.empty()) return frame;

    std::array<float, 3> low{ INFINITY, INFINITY, INFINITY };
    std::array<float, 3> high{ -INFINITY, -INFINITY, -INFINITY };
    for (const auto &point : mesh.positions) {
        for (size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], point[axis]);
            high[axis] = std::max(high[axis], point[axis]);
        }
    }
    const float extent = std::max({high[0] - low[0], high[1] - low[1],
                                   high[2] - low[2]});
    if (!std::isfinite(extent)) return frame;
    const float scale = extent > 0.000001f ? 2.0f / extent : 1.0f;
    const PreviewScene scene = ClampPreviewScene(input);
    aspect = std::isfinite(aspect) ? std::clamp(aspect, 0.25f, 4.0f) : 1;
    constexpr float pi = 3.14159265358979323846f;
    const float cy = std::cos(scene.yaw * pi / 180),
                sy = std::sin(scene.yaw * pi / 180);
    const float cp = std::cos(scene.pitch * pi / 180),
                sp = std::sin(scene.pitch * pi / 180);
    const size_t limit = std::min(mesh.indices.size() / 3 * 3,
        (kPreviewMaxSceneVertices - frame.vertices.size()) / 3 * 3);
    for (size_t i = 0; i < limit; ++i) {
        const uint32_t index = mesh.indices[i];
        if (index >= mesh.positions.size()) return frame;
        const auto &point = mesh.positions[index];
        const float px = (point[0] - (low[0] + high[0]) * 0.5f) * scale -
                         scene.target_pivot[0];
        const float py = (point[1] - (low[1] + high[1]) * 0.5f) * scale -
                         scene.target_pivot[1];
        const float pz = (point[2] - (low[2] + high[2]) * 0.5f) * scale -
                         scene.target_pivot[2];
        const float x = cy * px + sy * pz + scene.pan[0];
        const float z = -sy * px + cy * pz;
        const float camera_z = sp * py + cp * z - scene.distance;
        const float y = cp * py - sp * z + scene.pan[1];
        PreviewSceneVertex v{};
        constexpr float near = 0.1f, far = 32.0f;
        v.position[0] = 2.41421356f * x / aspect;
        v.position[1] = 2.41421356f * y * (vulkan ? -1 : 1);
        v.position[2] = -(far + near) / (far - near) * camera_z -
                        2 * far * near / (far - near);
        v.position[3] = -camera_z;
        if (vulkan) v.position[2] = (v.position[2] + v.position[3]) / 2;
        v.uv[0] = std::clamp((point[0] - low[0]) * scale * 0.5f, 0.0f, 1.0f);
        v.uv[1] = std::clamp((point[1] - low[1]) * scale * 0.5f, 0.0f, 1.0f);
        v.color[0] = v.uv[0];
        v.color[1] = v.uv[1];
        v.color[2] = 1.0f - v.uv[0];
        v.color[3] = 1.0f;
        frame.vertices.push_back(v);
    }
    target.vertex_count = static_cast<uint32_t>(frame.vertices.size()) -
                          target.first_vertex;
    return frame;
}
} // namespace xemu::shader_browser
