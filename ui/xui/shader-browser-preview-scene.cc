// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-scene.hh"
#include <algorithm>
#include <cmath>
namespace xemu::shader_browser {
bool PreviewScene::operator==(const PreviewScene &o) const
{
    return mesh == o.mesh && yaw == o.yaw && pitch == o.pitch &&
           distance == o.distance && pan == o.pan;
}
PreviewScene ClampPreviewScene(PreviewScene s)
{
    auto bound = [](float v, float lo, float hi, float fallback) {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    if (s.mesh != PreviewMesh::Quad && s.mesh != PreviewMesh::Sphere &&
        s.mesh != PreviewMesh::Cube)
        s.mesh = PreviewMesh::Quad;
    s.yaw = bound(s.yaw, -180, 180, 0);
    s.pitch = bound(s.pitch, -85, 85, 0);
    s.distance = bound(s.distance, 2.5f, 12, 4);
    for (auto &p : s.pan)
        p = bound(p, -2, 2, 0);
    return s;
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
} // namespace xemu::shader_browser
