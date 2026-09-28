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
    using ContextBinder = bool (*)(SDL_Window *, void *);
    explicit PreviewGlExecutor(ContextBinder bind_context = nullptr);
    ~PreviewGlExecutor();
    PreviewGlExecutor(const PreviewGlExecutor &) = delete;
    PreviewGlExecutor &operator=(const PreviewGlExecutor &) = delete;

    bool StartWhilePaused(std::string *error);
    void DrawImage(float max_width, float max_height,
                   const PreviewSelection *selection,
                   uint64_t now_ns, PreviewViewSettings *view,
                   PreviewScene *scene = nullptr);
    bool HasDisplayed() const;
    bool HasFrozen() const;
    bool NeedsRetirementPump() const;
    bool FreezeDisplayed();
    void ClearFrozen();
    void AfterHudRender();
    // Explicit asynchronous comparison readback on the producing worker.
    // Returns false without an error while owned pixels or terminal timing
    // are pending. The exact result is retained through consumer retirement;
    // returned rows are canonical top-down and require no HUD GL readback.
    bool CopyReadyImage(const PreviewResultKey &expected, uint32_t *width,
                        uint32_t *height, std::vector<uint8_t> *rgba,
                        std::string *error,
                        PreviewDrawTiming *draw_timing = nullptr);
    // Terminal context loss may skip GL deletion and leave objects to SDL
    // teardown.
    void Shutdown(bool have_shared_context = true);

private:
    struct Impl;
    Impl *impl_;
};

PreviewGlExecutor &GetPreviewGlExecutor();

} // namespace xemu::shader_browser
