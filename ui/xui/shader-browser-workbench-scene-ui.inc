// SPDX-License-Identifier: GPL-2.0-or-later
// Small shared ImGui helpers for the fixed synthetic scene. Include after imgui.h.
#include "shader-browser-preview-scene.hh"

namespace xemu::shader_browser {
struct WorkbenchSceneUiEdits {
    bool scene = false;
    bool render_state = false;
};

inline WorkbenchSceneUiEdits DrawWorkbenchSceneControls(
    PreviewScene *scene, PreviewRenderState *render_state,
    bool generated_alpha)
{
    WorkbenchSceneUiEdits edits{};
    if (!scene || !render_state) return edits;
    if (ImGui::CollapsingHeader("Scene and camera")) {
        int mesh = static_cast<int>(scene->mesh);
        edits.scene |= ImGui::Combo("Target", &mesh, "Quad\0Sphere\0Cube\0");
        scene->mesh = static_cast<PreviewMesh>(mesh);
        if (ImGui::Button("Focus target")) {
            *scene = ApplyPreviewCameraGesture(
                *scene, { PreviewCameraGestureKind::FocusTarget });
            edits.scene = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset camera")) {
            *scene = ApplyPreviewCameraGesture(
                *scene, { PreviewCameraGestureKind::ResetCamera });
            edits.scene = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset scene")) {
            *scene = ApplyPreviewCameraGesture(
                *scene, { PreviewCameraGestureKind::ResetScene });
            edits.scene = true;
        }
        edits.scene |= ImGui::DragFloat3("Target pivot",
                                         scene->target_pivot.data(), 0.02f,
                                         -1.5f, 1.5f);
        static const char *names[] = {
            "Backdrop", "Ground", "Intersection", "Blocker"
        };
        for (size_t i = 0; i < kPreviewReferenceCount; ++i) {
            auto &reference = scene->references[i];
            ImGui::PushID(static_cast<int>(i));
            edits.scene |= ImGui::Checkbox(names[i], &reference.visible);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170);
            edits.scene |= ImGui::DragFloat3("Offset",
                                             reference.translation.data(),
                                             0.02f, -0.75f, 0.75f);
            ImGui::PopID();
        }
        ImGui::TextDisabled(
            "Left drag orbit, middle or Shift+left drag pan, wheel dolly, F focus");
    }
    if (ImGui::CollapsingHeader("Synthetic render state")) {
        int blend = static_cast<int>(render_state->blend);
        edits.render_state |= ImGui::Combo("Blend", &blend,
                                           "Opaque\0Alpha\0");
        render_state->blend = static_cast<PreviewBlendMode>(blend);
        edits.render_state |= ImGui::Checkbox("Depth test",
                                              &render_state->depth_test);
        edits.render_state |= ImGui::Checkbox("Depth write",
                                              &render_state->depth_write);
        int cull = static_cast<int>(render_state->cull);
        edits.render_state |= ImGui::Combo("Cull", &cull,
                                           "None\0Back\0Front\0");
        render_state->cull = static_cast<PreviewCullMode>(cull);
        ImGui::BeginDisabled();
        edits.render_state |= ImGui::Checkbox("Alpha test",
                                              &render_state->alpha_test);
        ImGui::EndDisabled();
        int alpha = render_state->alpha_reference;
        edits.render_state |= ImGui::SliderInt("Alpha reference", &alpha,
                                               0, 255);
        render_state->alpha_reference = static_cast<uint8_t>(alpha);
        edits.render_state |= ImGui::ColorEdit4(
            "Initial destination", render_state->clear_color.data());
        ImGui::TextDisabled(generated_alpha ?
            "Generated GLSL bakes alpha discard from its recipe" :
            "Edited/replacement GLSL defines its own alpha discard");
    }
    if (edits.scene) *scene = ClampPreviewScene(*scene);
    if (edits.render_state)
        *render_state = ClampPreviewRenderState(*render_state);
    return edits;
}

inline bool HandleWorkbenchViewportGesture(PreviewScene *scene,
                                           const ImVec2 &position,
                                           const ImVec2 &size)
{
    if (!scene || size.x <= 1 || size.y <= 1) return false;
    const ImVec2 after_image = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(position);
    ImGui::InvisibleButton("##workbench-camera", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    ImGui::SetCursorScreenPos(after_image);
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return false;
    const ImGuiIO &io = ImGui::GetIO();
    PreviewScene next = *scene;
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        const bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                         io.KeyShift;
        next = ApplyPreviewCameraGesture(
            next, { pan ? PreviewCameraGestureKind::Pan :
                          PreviewCameraGestureKind::Orbit,
                    io.MouseDelta.x / size.x, io.MouseDelta.y / size.y });
    }
    if (hovered && io.MouseWheel != 0)
        next = ApplyPreviewCameraGesture(
            next, { PreviewCameraGestureKind::Dolly, 0, io.MouseWheel });
    if ((focused || active) && ImGui::IsKeyPressed(ImGuiKey_F))
        next = ApplyPreviewCameraGesture(
            next, { PreviewCameraGestureKind::FocusTarget });
    if (next == *scene) return false;
    *scene = next;
    return true;
}
} // namespace xemu::shader_browser
