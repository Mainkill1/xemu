// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-selection-ui.hh"
#include "../../ui/xui/shader-browser-stage3-ui.hh"
#include <cassert>
#include <cstdio>
using namespace xemu::shader_browser;
static int choice;
static int changes;
static ImVec2 combo;
static void Frame()
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Always);
    ImGui::Begin("Selection");
    const char *items[] = { "Flat",         "UV",     "Checker", "Alpha",
                            "MultiTexture", "Normal", "Cubemap", "Fog",
                            "Diagnostic",   "Custom" };
    if (ShaderCombo("Profile", &choice, items, 10))
        ++changes;
    combo = ImGui::GetItemRectMin();
    combo.x += 20;
    combo.y += 8;
    ImGui::End();
    ImGui::Render();
}
static void Key(ImGuiKey key)
{
    ImGui::GetIO().AddKeyEvent(key, true);
    Frame();
    ImGui::GetIO().AddKeyEvent(key, false);
    Frame();
    Frame();
}
static void Open()
{
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(combo.x, combo.y);
    Frame();
    io.AddMouseButtonEvent(0, true);
    Frame();
    io.AddMouseButtonEvent(0, false);
    Frame();
    Frame();
}
int main()
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1000, 700);
    io.DeltaTime = 1.0f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    Frame();
    Frame();
    Frame();
    Open();
    Key(ImGuiKey_DownArrow);
    Key(ImGuiKey_DownArrow);
    std::printf("choice after two Down keys without Enter: %d\n", choice);
    assert(choice == 2);
    Key(ImGuiKey_UpArrow);
    assert(choice == 1);
    const int before_escape = changes;
    Key(ImGuiKey_Escape);
    assert(choice == 0);
    assert(changes == before_escape + 1);
    Open();
    Key(ImGuiKey_End);
    assert(choice == 9);
    Key(ImGuiKey_Home);
    assert(choice == 0);
    Key(ImGuiKey_PageDown);
    assert(choice > 0);
    Key(ImGuiKey_PageUp);
    assert(choice == 0);
    io.AddInputCharactersUTF8("c");
    Frame();
    Frame();
    Frame();
    assert(choice == 2);
    io.AddInputCharactersUTF8("u");
    Frame();
    Frame();
    Frame();
    assert(choice == 6); // Cubemap: typing previews without Enter.
    Key(ImGuiKey_Escape);
    assert(choice == 0);
    choice = 9;
    Frame();
    Open();
    auto *popup = ImGui::GetCurrentContext()->NavWindow;
    assert(popup && popup->Scroll.y > 0);
    Key(ImGuiKey_UpArrow);
    assert(choice == 8);
    Key(ImGuiKey_Enter);
    assert(choice == 8); // Enter accepts the preview instead of restoring it.
    Open();
    Key(ImGuiKey_UpArrow);
    assert(choice == 7);
    Key(ImGuiKey_Escape);
    assert(choice == 8); // Each opening owns its own original choice.
    // Drive an actual catalog-style mouse drag through ImGui's shared tag,
    // owned identity copy and delivery to the replacement target.
    auto &drag_io = ImGui::GetIO();
    ImVec2 source_pos, target_pos;
    bool delivered = false;
    auto drag_frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Always);
        ImGui::Begin("Catalog drag contract");
        ImGui::Selectable("Catalog shader", true,
                          ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(200, 40));
        source_pos = ImGui::GetItemRectMin();
        source_pos.x += 30;
        source_pos.y += 20;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ShaderDragPayload identity{};
            identity.title_id = 0x12345678;
            identity.identity_version = 1;
            identity.shader_hash[0] = 42;
            identity.stage = static_cast<uint32_t>(Stage::Pixel);
            ImGui::SetDragDropPayload(kShaderDragPayloadType, &identity,
                                      sizeof(identity));
            identity.shader_hash[0] = 99;
            const auto *payload = ImGui::GetDragDropPayload();
            assert(payload && payload->IsDataType(kShaderDragPayloadType));
            assert(payload->DataSize == sizeof(ShaderDragPayload));
            const auto *copied =
                static_cast<const ShaderDragPayload *>(payload->Data);
            assert(copied->title_id == 0x12345678 &&
                   copied->shader_hash[0] == 42);
            ImGui::EndDragDropSource();
        }
        ImGui::Button("Replacement target", ImVec2(200, 40));
        target_pos = ImGui::GetItemRectMin();
        target_pos.x += 30;
        target_pos.y += 20;
        if (ImGui::BeginDragDropTarget()) {
            if (const auto *payload =
                    ImGui::AcceptDragDropPayload(kShaderDragPayloadType)) {
                assert(payload->DataSize == sizeof(ShaderDragPayload));
                const auto *copied =
                    static_cast<const ShaderDragPayload *>(payload->Data);
                assert(copied->title_id == 0x12345678 &&
                       copied->shader_hash[0] == 42);
                delivered = payload->IsDelivery();
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::End();
        ImGui::Render();
    };
    drag_frame();
    drag_frame();
    drag_io.AddMousePosEvent(source_pos.x, source_pos.y);
    drag_frame();
    drag_io.AddMouseButtonEvent(0, true);
    drag_frame();
    drag_io.AddMousePosEvent(source_pos.x + 30, source_pos.y);
    drag_frame();
    drag_io.AddMousePosEvent(target_pos.x, target_pos.y);
    drag_frame();
    drag_frame();
    assert(!delivered); // Browsing the target cannot apply a replacement.
    drag_io.AddMouseButtonEvent(0, false);
    drag_frame();
    drag_frame();
    assert(delivered);
    ImGui::DestroyContext();
}
