// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-selection-ui.hh"
#include <cassert>
#include <cstdio>
using namespace xemu::shader_browser;
static int choice;
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
    ShaderCombo("Profile", &choice, items, 10);
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
int main()
{
    ImGui::CreateContext();
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
    io.AddMousePosEvent(combo.x, combo.y);
    Frame();
    io.AddMouseButtonEvent(0, true);
    Frame();
    io.AddMouseButtonEvent(0, false);
    Frame();
    Frame();
    Key(ImGuiKey_DownArrow);
    Key(ImGuiKey_DownArrow);
    std::printf("choice after two Down keys without Enter: %d\n", choice);
    assert(choice == 2);
    Key(ImGuiKey_UpArrow);
    assert(choice == 1);
    Key(ImGuiKey_Escape);
    assert(choice == 1);
    ImGui::DestroyContext();
}
