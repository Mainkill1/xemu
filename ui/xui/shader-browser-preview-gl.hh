// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-model.hh"

#include <array>
#include <cstdint>
#include <string>

struct SDL_Window;

namespace xemu::shader_browser {

// Unlike SDL_GL_MakeCurrent, a null pair is an error, never a valid unbind.
bool MakePreviewHudContextCurrent(SDL_Window *window, void *context);

struct PreviewViewSettings {
    float zoom = 1.0f;
    std::array<float, 2> center{0.5f, 0.5f};
};

// Owns a GL context shared only for final HUD texture presentation. All
// shader compilation, drawing, and producer completion run on its worker.
class PreviewGlExecutor
{
public:
    PreviewGlExecutor();
    ~PreviewGlExecutor();
    PreviewGlExecutor(const PreviewGlExecutor &) = delete;
    PreviewGlExecutor &operator=(const PreviewGlExecutor &) = delete;

    bool StartWhilePaused(std::string *error);
    void DrawImage(float side, const PreviewSelection *selection,
                   uint64_t now_ns, PreviewViewSettings *view);
    bool HasDisplayed() const;
    bool HasFrozen() const;
    bool NeedsRetirementPump() const;
    bool FreezeDisplayed();
    void ClearFrozen();
    void AfterHudRender();
    // Terminal context loss may skip GL deletion and leave objects to SDL
    // teardown.
    void Shutdown(bool have_shared_context = true);

private:
    struct Impl;
    Impl *impl_;
};

PreviewGlExecutor &GetPreviewGlExecutor();

} // namespace xemu::shader_browser
