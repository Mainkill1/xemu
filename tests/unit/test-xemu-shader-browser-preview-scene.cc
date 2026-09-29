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
    PreviewScene fixture;
    auto frame = BuildPreviewSceneFrame(fixture, 16.0f / 9.0f);
    assert(frame.draw_count == kPreviewMaxSceneDraws);
    assert(frame.vertices.size() <= kPreviewMaxSceneVertices);
    uint32_t next = 0;
    for (size_t i = 0; i < frame.draw_count; ++i) {
        const auto &draw = frame.draws[i];
        assert(draw.role == static_cast<PreviewSceneRole>(i));
        assert(draw.first_vertex == next);
        assert(draw.vertex_count > 0 && draw.vertex_count % 3 == 0);
        next += draw.vertex_count;
    }
    assert(next == frame.vertices.size());
    fixture.target_pivot[0] = 0.5f;
    auto shifted = BuildPreviewSceneFrame(fixture, 16.0f / 9.0f);
    assert(shifted.vertices[frame.draws[0].first_vertex].position[0] !=
           frame.vertices[frame.draws[0].first_vertex].position[0]);
    assert(shifted.vertices[frame.draws[4].first_vertex].position[0] !=
           frame.vertices[frame.draws[4].first_vertex].position[0]);
    fixture.target_pivot = {};
    const auto &blocker = frame.draws[3];
    const auto &target = frame.draws[4];
    assert(frame.vertices[blocker.first_vertex].position[3] <
           frame.vertices[target.first_vertex].position[3]);
    fixture.references[3].visible = false;
    auto hidden = BuildPreviewSceneFrame(fixture);
    assert(hidden.draws[3].vertex_count == 0);
    fixture.references[3].visible = true;
    fixture.references[3].translation[0] = 0.5f;
    auto moved = BuildPreviewSceneFrame(fixture);
    assert(moved.vertices[blocker.first_vertex].position[0] !=
           frame.vertices[blocker.first_vertex].position[0]);
    for (auto kind : { PreviewMesh::Quad, PreviewMesh::Sphere,
                       PreviewMesh::Cube }) {
        fixture.mesh = kind;
        auto built = BuildPreviewSceneFrame(fixture);
        assert(built.draws[4].vertex_count ==
               (kind == PreviewMesh::Quad ? 6U :
                kind == PreviewMesh::Cube ? 36U : 32U * 16U * 6U));
        assert(built.vertices.size() <= kPreviewMaxSceneVertices);
    }
    fixture.pan = { 0.4f, -0.3f };
    const auto focused = ApplyPreviewCameraGesture(
        fixture, { PreviewCameraGestureKind::FocusTarget });
    assert(focused.pan[0] == 0 && focused.pan[1] == 0);
    assert(focused.target_pivot[0] == 0 && focused.target_pivot[1] == 0 &&
           focused.target_pivot[2] == 0);
    assert(focused.mesh == fixture.mesh);
    const auto orbit = ApplyPreviewCameraGesture(
        fixture, { PreviewCameraGestureKind::Orbit, 0.1f, 0.1f });
    assert(orbit.yaw != fixture.yaw && orbit.pitch != fixture.pitch);
    const auto panned = ApplyPreviewCameraGesture(
        fixture, { PreviewCameraGestureKind::Pan, 0.1f, 0.1f });
    assert(panned.pan != fixture.pan);
    const auto dolly = ApplyPreviewCameraGesture(
        fixture, { PreviewCameraGestureKind::Dolly, 0, 1 });
    assert(dolly.distance < fixture.distance);
    assert(ApplyPreviewCameraGesture(
               fixture, { PreviewCameraGestureKind::ResetScene }) ==
           PreviewScene{});
    fixture.references[0].translation[0] =
        std::numeric_limits<float>::infinity();
    fixture.target_pivot[0] = std::numeric_limits<float>::quiet_NaN();
    auto clamped = ClampPreviewScene(fixture);
    assert(std::isfinite(clamped.references[0].translation[0]));
    assert(std::isfinite(clamped.target_pivot[0]));
    auto gl = BuildPreviewSceneFrame(clamped, 2, false);
    auto vk_frame = BuildPreviewSceneFrame(clamped, 2, true);
    assert(gl.vertices.size() == vk_frame.vertices.size());
    for (size_t i = 0; i < gl.vertices.size(); ++i) {
        assert(gl.vertices[i].position[1] ==
               -vk_frame.vertices[i].position[1]);
        assert(std::abs(vk_frame.vertices[i].position[2] -
                        (gl.vertices[i].position[2] +
                         gl.vertices[i].position[3]) / 2) < 1e-5);
    }
    PreviewRenderState state;
    state.clear_color[0] = std::numeric_limits<float>::infinity();
    state.cull = static_cast<PreviewCullMode>(255);
    state.alpha_test = true;
    state = ClampPreviewRenderState(state);
    assert(std::isfinite(state.clear_color[0]));
    assert(state.cull == PreviewCullMode::None);
    assert(state.alpha_test);
    std::cout << "Preview scene tests passed\n";
}
