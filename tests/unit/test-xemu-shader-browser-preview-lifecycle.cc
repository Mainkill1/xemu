// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-gl.hh"
#include "shader-browser-preview-service.hh"
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <glib.h>
#include <cassert>
#include <cstdio>
#include <set>
#include <vector>

using namespace xemu::shader_browser;
static PreviewBackend backend = PreviewBackend::OpenGL;
static uint64_t Now()
{
    return g_get_monotonic_time() * UINT64_C(1000);
}
static PreviewPacket Packet(PreviewMode mode, bool bad, uint64_t revision)
{
    PreviewPacket p;
    p.selection.scope.title_id = 1;
    p.selection.session_epoch = 1;
    p.selection.renderer_epoch = 1;
    p.selection.backend = backend;
    p.selection.mode = mode;
    p.selection.shader.stage = Stage::Pixel;
    p.recipe_format_version = 1;
    p.recipe = { 1, 2, 3, 4 };
    p.selection.shader.hash =
        ComputeShaderHash(1, Stage::Pixel, 1, p.recipe.data(), p.recipe.size());
    p.generator_abi = p.interface_abi = 1;
    p.width = p.height = 160;
    p.source = backend == PreviewBackend::Vulkan ?
                   "#version 450\nlayout(location=0) out vec4 color;\n" :
                   "#version 400\nout vec4 color;\n";
    p.source += bad ? "invalid shader" :
                mode == PreviewMode::Normal ?
                      "void main(){color=vec4(1,0,0,1);}" :
                      "void main(){color=vec4(0,1,0,1);}";
    p.partner_source =
        BuildPreviewSyntheticVertexSource(p.source, p.selection.backend);
    p.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(p.source.data()), p.source.size());
    p.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(p.partner_source.data()),
        p.partner_source.size());
    p.fixture_bytes = EncodePreviewSyntheticFixture(PreviewSyntheticFixture{});
    p.fixture_digest =
        ComputePreviewDigest(p.fixture_bytes.data(), p.fixture_bytes.size());
    if (mode == PreviewMode::Replacement) {
        p.replacement_id = 4;
        p.replacement_revision = revision;
    }
    return p;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "vulkan")
        backend = PreviewBackend::Vulkan;
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    auto *window = SDL_CreateWindow("Preview lifecycle", 800, 600,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    auto context = SDL_GL_CreateContext(window);
    assert(context);
    // Model the main/external HUD share group. All image sampling below uses
    // context; cleanup must switch back to that consumer before Shutdown.
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    auto *main_window = SDL_CreateWindow("Main HUD context", 64, 64,
                                         SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(main_window);
    auto main_context = SDL_GL_CreateContext(main_window);
    assert(main_context);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    assert(SDL_GL_MakeCurrent(window, context));
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(800, 600);
    assert(ImGui_ImplOpenGL3_Init("#version 400"));
    auto &service = GetPreviewService();
    PreviewGlExecutor executor;
    auto packet = Packet(PreviewMode::Normal, false, 0);
    service.SetEnabled(true);
    service.SetVisible(true, Now());
    service.SetGuestPaused(true);
    std::string error;
    auto submit = [&] {
        fprintf(stderr, "Submit mode %d epoch %llu\n",
                int(packet.selection.mode),
                (unsigned long long)packet.selection.renderer_epoch);
        service.SetVisible(true, Now());
        service.SetSelection(packet.selection, Now());
        assert(service.SubmitPacket(packet, Now(), &error));
    };
    submit();
    std::set<GLuint> textures;
    auto draw = [&] {
        service.SetVisible(true, Now());
        if (service.RequestAutomaticPreparation(Now()))
            assert(executor.StartWhilePaused(&error));
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::Begin("Preview");
        executor.DrawImage(600, &packet.selection, Now(), nullptr);
        ImGui::End();
        ImGui::Render();
        textures.clear();
        auto *data = ImGui::GetDrawData();
        const auto font = ImGui::GetIO().Fonts->TexID;
        for (int i = 0; i < data->CmdListsCount; ++i)
            for (const auto &cmd : data->CmdLists[i]->CmdBuffer)
                if (cmd.GetTexID() != font && cmd.GetTexID())
                    textures.insert(static_cast<GLuint>(cmd.GetTexID()));
        glViewport(0, 0, 800, 600);
        ImGui_ImplOpenGL3_RenderDrawData(data);
        executor.AfterHudRender();
        const GLenum gl_error = glGetError();
        if (gl_error)
            fprintf(stderr, "HUD GL error: %x\n", gl_error);
        assert(gl_error == GL_NO_ERROR);
    };
    auto await = [&](auto predicate) {
        const auto deadline = Now() + UINT64_C(10000000000);
        do {
            draw();
            if (predicate())
                return;
            SDL_Delay(10);
        } while (Now() < deadline);
        PreviewStatus status;
        service.CopyStatus(&status);
        fprintf(stderr, "Timeout: %s\n", status.message.c_str());
        assert(false);
    };
    auto colors = [&] {
        std::set<unsigned> result;
        for (GLuint texture : textures) {
            glBindTexture(GL_TEXTURE_2D, texture);
            std::vector<uint8_t> rgba(160 * 160 * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                          rgba.data());
            const size_t center = (80 * 160 + 80) * 4;
            result.insert((unsigned(rgba[center]) << 16) |
                          (unsigned(rgba[center + 1]) << 8) | rgba[center + 2]);
        }
        return result;
    };
    await([&] { return executor.HasDisplayed(); });
    assert(colors() == std::set<unsigned>{ 0xff0000 });
    // Clearing a Reference must preserve the sole last-good frame when the
    // failed attempt cannot produce an independent Current.
    packet = Packet(PreviewMode::Replacement, true, 2);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    PreviewStatus failed_status;
    service.CopyStatus(&failed_status);
    assert(failed_status.leased_slots == 1 && failed_status.free_slots == 2);
    assert(failed_status.attempted_compile.replacement_revision == 2);
    assert(executor.FreezeDisplayed());
    draw();
    assert(executor.HasFrozen() && !executor.HasDisplayed());
    assert(colors() == std::set<unsigned>{ 0xff0000 });
    executor.ClearFrozen();
    assert(executor.HasDisplayed() && !executor.HasFrozen());
    draw();
    assert(textures.size() == 1);
    assert(colors() == std::set<unsigned>{ 0xff0000 });
    PreviewStatus cleared_status;
    service.CopyStatus(&cleared_status);
    assert(cleared_status.state == PreviewState::Failed);
    assert(cleared_status.message == failed_status.message);
    assert(cleared_status.attempted_compile == failed_status.attempted_compile);
    assert(cleared_status.leased_slots == 1 && cleared_status.free_slots == 2);
    packet = Packet(PreviewMode::Normal, false, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Ready;
    });
    assert(executor.FreezeDisplayed());
    await([&] { return executor.HasDisplayed() && executor.HasFrozen(); });
    assert(textures.size() == 2);
    packet = Packet(PreviewMode::Replacement, true, 2);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    assert(executor.HasDisplayed() && executor.HasFrozen());
    assert(textures.size() == 2);
    assert(colors() == std::set<unsigned>{ 0xff0000 });
    packet = Packet(PreviewMode::Replacement, false, 3);
    submit();
    await([&] { return colors() == std::set<unsigned>{ 0xff0000, 0x00ff00 }; });
    packet = Packet(PreviewMode::Uber, true, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    assert(colors() == std::set<unsigned>({ 0xff0000, 0x00ff00 }));
    packet = Packet(PreviewMode::Uber, false, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Ready && status.prepared;
    });
    draw();
    assert(executor.HasFrozen());
    // A malformed private fixture fails during rendering, preserving both
    // images.
    packet.fixture_bytes = { 0 };
    packet.fixture_digest = ComputePreviewDigest(packet.fixture_bytes.data(),
                                                 packet.fixture_bytes.size());
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    assert(colors() == std::set<unsigned>({ 0xff0000, 0x00ff00 }));
    packet = Packet(PreviewMode::Uber, false, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Ready;
    });
    // A renderer epoch ends both comparisons even before new preparation.
    ++packet.selection.renderer_epoch;
    submit();
    draw();
    assert(!executor.HasDisplayed() && !executor.HasFrozen());
    await([&] { return executor.HasDisplayed(); });
    assert(executor.FreezeDisplayed());
    await([&] { return executor.HasDisplayed(); });
    draw(); // Submit actual HUD sampling immediately before terminal shutdown.
    assert(SDL_GL_MakeCurrent(main_window, main_context));
    assert(SDL_GL_GetCurrentContext() == main_context);
    assert(SDL_GL_MakeCurrent(window, context));
    executor.Shutdown();
    assert(SDL_GL_GetCurrentContext() == context);
    assert(SDL_GL_MakeCurrent(main_window, main_context));
    assert(SDL_GL_GetCurrentContext() == main_context);
    assert(glGetError() == GL_NO_ERROR);
    assert(SDL_GL_MakeCurrent(window, context));
    assert(!executor.HasDisplayed() && !executor.HasFrozen());
    assert(!executor.NeedsRetirementPump());
    // Reuse the owner after a terminal reset; old GL object names cannot
    // escape.
    service.SetEnabled(true);
    ++packet.selection.renderer_epoch;
    submit();
    await([&] { return executor.HasDisplayed(); });
    service.SetEnabled(false);
    draw();
    assert(!executor.HasDisplayed() && !executor.HasFrozen());
    executor.Shutdown();
    assert(glGetError() == GL_NO_ERROR);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(main_context);
    SDL_DestroyWindow(main_window);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Backend %d: ", int(backend));
    puts("Production preview worker/HUD lifecycle: last-good pixels, "
         "cross-mode reference, auto paused preparation, epoch reset, disable, "
         "shutdown/restart passed");
}
