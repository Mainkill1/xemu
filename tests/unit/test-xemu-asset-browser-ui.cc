// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/asset-browser.hh"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <glib.h>
using namespace xemu::asset_browser;
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_assert_true(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    auto *window = SDL_CreateWindow("Asset UI fixture", 1200, 800,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    g_assert_nonnull(window);
    auto gl = SDL_GL_CreateContext(window);
    g_assert_nonnull(gl);
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = { 1200, 800 };
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    capture::CaptureSession session;
    capture::CaptureSessionContext context;
    context.scope.title_id = 17;
    context.session_epoch = 1;
    context.renderer_epoch = 2;
    context.generation = 4;
    context.scope_generation = 3;
    context.backend = 2;
    AssetBrowserWindow browser(
        session, [&] { return context; },
        [](auto, const auto &, auto) { return std::string{}; });
    AssetCatalog catalog;
    catalog.context = context;
    catalog.frame = 12;
    catalog.complete_frame = true;
    for (uint64_t id = 1; id <= 32; ++id) {
        auto part = std::make_shared<AssetPart>();
        part->id = id;
        part->frame = 12;
        part->status = AssetStatus::Ready;
        part->vertices.resize(3);
        part->vertices[1].position = { 1, 0, 0 };
        part->vertices[2].position = { 0, 1, 0 };
        part->indices = { 0, 1, 2 };
        part->bounds.valid = true;
        part->bounds.maximum = { 1, 1, 0 };
        catalog.parts.push_back(part);
        catalog.entries.push_back(
            MakeAssetAssembly(catalog, { id }, "Part " + std::to_string(id)));
    }
    auto next_catalog = catalog;
    next_catalog.frame = 13;
    next_catalog.entries.clear();
    for (auto &part : next_catalog.parts) {
        auto newer = std::make_shared<AssetPart>(*part);
        newer->frame = 13;
        part = newer;
    }
    for (const auto &part : next_catalog.parts)
        next_catalog.entries.push_back(MakeAssetAssembly(
            next_catalog, { part->id }, "Part " + std::to_string(part->id)));
    g_assert_true(browser.InspectCatalog(std::move(catalog)));
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
        browser.Draw();
        ImGui::Render();
    };
    frame();
    ImGui::SetWindowFocus("Asset Browser");
    frame();
    io.AddKeyEvent(ImGuiKey_DownArrow, true);
    frame();
    g_assert_nonnull(browser.Selected().get());
    g_assert_cmpuint(browser.Selected()->id, ==, 1);
    io.AddKeyEvent(ImGuiKey_DownArrow, false);
    frame();
    io.AddKeyEvent(ImGuiKey_DownArrow, true);
    frame();
    g_assert_cmpuint(browser.Selected()->id, ==, 2);
    io.AddKeyEvent(ImGuiKey_DownArrow, false);
    frame();
    io.AddKeyEvent(ImGuiKey_End, true);
    frame();
    g_assert_cmpuint(browser.Selected()->id, ==, 32);
    io.AddKeyEvent(ImGuiKey_End, false);
    frame();
    g_assert_true(browser.InspectCatalog(std::move(next_catalog)));
    frame();
    io.AddKeyEvent(ImGuiKey_DownArrow, true);
    frame();
    g_assert_cmpuint(browser.Selected()->frame, ==, 13);
    g_assert_cmpuint(browser.Selected()->id, ==, 1);
    io.AddKeyEvent(ImGuiKey_DownArrow, false);
    frame();
    auto frozen = browser.Selected();
    g_assert_true(session.Start(context));
    std::vector<GLuint> textures;
    for (const auto *list : ImGui::GetDrawData()->CmdLists)
        for (const auto &command : list->CmdBuffer) {
            const auto texture = GLuint((intptr_t)command.GetTexID());
            if (texture && glIsTexture(texture))
                textures.push_back(texture);
        }
    g_assert_false(textures.empty());
    ImGui::SetWindowCollapsed("Asset Browser", true);
    frame();
    g_assert_true(session.Active());
    for (auto texture : textures)
        g_assert_false(glIsTexture(texture));
    ImGui::SetWindowCollapsed("Asset Browser", false);
    frame();
    browser.m_is_open = false;
    frame();
    g_assert_true(session.Active());
    g_assert_true(browser.Selected() == frozen);
    auto occurrence = std::make_shared<capture::CaptureOccurrence>();
    occurrence->event_id = 100;
    occurrence->pending = false;
    occurrence->emitted = true;
    occurrence->summary.primitive_mode = 5;
    occurrence->summary.key.frame = 12;
    capture::CaptureOwnedBlob positions;
    positions.name = "vertex.attribute0";
    positions.count = 3;
    positions.components = 3;
    positions.stride = 12;
    positions.format = 106;
    auto bytes = std::make_shared<capture::CaptureImmutableBlock>();
    const float vertices[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    const auto *raw = reinterpret_cast<const uint8_t *>(vertices);
    bytes->bytes.assign(raw, raw + sizeof(vertices));
    positions.data = bytes;
    occurrence->inputs.blobs.push_back(positions);
    g_assert_true(browser.InspectOccurrence(occurrence, context));
    for (int i = 0;
         i < 100 && (!browser.Selected() || browser.Selected()->id != 100);
         ++i) {
        frame();
        SDL_Delay(1);
    }
    g_assert_nonnull(browser.Selected().get());
    g_assert_cmpuint(browser.Selected()->id, ==, 100);
    g_assert_false(browser.Selected()->user_confirmed);
    g_assert_true(session.Active()); // Inspector cannot steal another recorder.
    browser.Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    g_print("32-use immediate keyboard selection and foreign-recorder close "
            "fixture passed\n");
    return 0;
}
