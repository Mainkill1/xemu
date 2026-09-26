// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-model.hh"

#include <array>
#include <cstdint>
#include <string>

struct SDL_Window;

namespace xemu::shader_browser {

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
    void Shutdown();

private:
    struct Impl;
    Impl *impl_;
};

PreviewGlExecutor &GetPreviewGlExecutor();

} // namespace xemu::shader_browser
