// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-preview-scene.hh"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
using namespace xemu::shader_browser;
int main()
{
    PreviewScene scene;
    auto quad = BuildPreviewSceneGeometry(scene);
    assert(quad.size() == 6);
    for (auto mesh :
         { PreviewMesh::Quad, PreviewMesh::Sphere, PreviewMesh::Cube }) {
        scene.mesh = mesh;
        scene.yaw = 37;
        scene.pitch = 21;
        auto a = BuildPreviewSceneGeometry(scene);
        assert(!a.empty() && a.size() <= kPreviewMaxSceneVertices &&
               a.size() % 3 == 0);
        auto vk = BuildPreviewSceneGeometry(scene, 1, true);
        assert(vk.size() == a.size());
        for (size_t i = 0; i < a.size(); ++i) {
            for (float c : a[i].position)
                assert(std::isfinite(c));
            assert(a[i].position[3] > 0.1f);
            assert(a[i].position[1] == -vk[i].position[1]);
            assert(std::abs(vk[i].position[2] -
                            (a[i].position[2] + a[i].position[3]) / 2) < 1e-5);
        }
        for (size_t i = 0; i < a.size(); i += 3) {
            auto x = [&](int j) {
                return a[i + j].position[0] / a[i + j].position[3];
            };
            auto y = [&](int j) {
                return a[i + j].position[1] / a[i + j].position[3];
            };
            assert((x(1) - x(0)) * (y(2) - y(0)) -
                       (y(1) - y(0)) * (x(2) - x(0)) >
                   0);
        }
        scene.distance = 6;
        auto b = BuildPreviewSceneGeometry(scene);
        assert(a[0].position[3] != b[0].position[3]);
        scene.distance = 3;
        scene.pan[0] = 0.5f;
        auto c = BuildPreviewSceneGeometry(scene);
        assert(a[0].position[0] != c[0].position[0]);
        scene.pan = {};
    }
    scene.distance = -1;
    scene.pitch = 999;
    scene.yaw = std::numeric_limits<float>::infinity();
    scene.pan[0] = std::numeric_limits<float>::quiet_NaN();
    scene = ClampPreviewScene(scene);
    assert(scene.distance >= 2.5f && scene.pitch <= 85 &&
           std::isfinite(scene.yaw) && std::isfinite(scene.pan[0]));
    std::cout << "Preview scene tests passed\n";
}
